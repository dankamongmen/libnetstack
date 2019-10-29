#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <netlink/msg.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <netlink/socket.h>
#include <netlink/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include "netstack.h"

typedef struct netstack {
  struct nl_sock* nl;  // netlink connection abstraction from libnl
  pthread_t rxtid;
  pthread_t txtid;
  // We can only have one command of the class e.g. DUMP outstanding at a time.
  // Queue up any others for transmission when possible. -1 == end of queue.
  int txqueue[128];
  pthread_cond_t txcond;
  pthread_mutex_t txlock;
} netstack;

typedef struct netstack_iface {
  char name[IFNAMSIZ];
  // FIXME
} netstack_iface;

// Sits on blocking nl_recvmsgs()
static void*
netstack_rx_thread(void* vns){
  netstack* ns = vns;
  int ret;
  while((ret = nl_recvmsgs_default(ns->nl)) == 0){
    printf("Got a netlink message!\n"); // FIXME
  }
  fprintf(stderr, "Error receiving from netlink socket (%s)\n",
          nl_geterror(ret));
  // FIXME recover?
  return NULL;
}

static void
tx_cancel_clean(void* vns){
  netstack* ns = vns;
  pthread_mutex_unlock(&ns->txlock);
}

// Sits on condition variable, transmitting when there's data in the txqueue
static void*
netstack_tx_thread(void* vns){
  netstack* ns = vns;
  pthread_mutex_lock(&ns->txlock);
  while(ns->txqueue[0] == -1){
    pthread_cleanup_push(tx_cancel_clean, ns);
    pthread_cond_wait(&ns->txcond, &ns->txlock);
    pthread_cleanup_pop(0);
  }
  int i = 0;
  do{
    struct rtgenmsg rt = {
      .rtgen_family = AF_UNSPEC,
    };
    if(nl_send_simple(ns->nl, ns->txqueue[i], NLM_F_REQUEST|NLM_F_DUMP, &rt, sizeof(rt)) < 0){
      // FIXME do what?
    }
  }while(ns->txqueue[++i] != -1);
  ns->txqueue[0] = -1;
  pthread_mutex_unlock(&ns->txlock);
  return NULL;
}

static bool
link_rta_handler(netstack_iface* ni, const struct rtattr* rta, int rlen){
  switch(rta->rta_type){
    case IFLA_ADDRESS:
      // FIXME copy L2 ucast address
      break;
    case IFLA_BROADCAST:
      // FIXME copy L2 bcast address
      break;
    case IFLA_IFNAME:
      strcpy(ni->name, RTA_DATA(rta)); // FIXME rigourize
      break;
    case IFLA_MTU:
      // FIXME copy mtu
      break;
    case IFLA_LINK: case IFLA_QDISC: case IFLA_STATS:
      // FIXME
      break;
    case IFLA_COST:
    case IFLA_PRIORITY:
    case IFLA_MASTER:
    case IFLA_WIRELESS:		/* Wireless Extension event - see wireless.h */
    case IFLA_PROTINFO:		/* Protocol specific information for a link */
    case IFLA_TXQLEN:
    case IFLA_MAP:
    case IFLA_WEIGHT:
    case IFLA_OPERSTATE:
    case IFLA_LINKMODE:
    case IFLA_LINKINFO:
    case IFLA_NET_NS_PID:
    case IFLA_IFALIAS:
    case IFLA_NUM_VF:		/* Number of VFs if device is SR-IOV PF */
    case IFLA_VFINFO_LIST:
    case IFLA_STATS64:
    case IFLA_VF_PORTS:
    case IFLA_PORT_SELF:
    case IFLA_AF_SPEC:
    case IFLA_GROUP:		/* Group the device belongs to */
    case IFLA_NET_NS_FD:
    case IFLA_EXT_MASK:		/* Extended info mask: VFs: etc */
    case IFLA_PROMISCUITY:	/* Promiscuity count: > 0 means acts PROMISC */
    case IFLA_NUM_TX_QUEUES:
    case IFLA_NUM_RX_QUEUES:
    case IFLA_CARRIER:
    case IFLA_PHYS_PORT_ID:
    case IFLA_CARRIER_CHANGES:
    case IFLA_PHYS_SWITCH_ID:
    case IFLA_LINK_NETNSID:
    case IFLA_PHYS_PORT_NAME:
    case IFLA_PROTO_DOWN:
    case IFLA_GSO_MAX_SEGS:
    case IFLA_GSO_MAX_SIZE:
    case IFLA_PAD:
    case IFLA_XDP:
    case IFLA_EVENT:
    case IFLA_NEW_NETNSID:
    case IFLA_TARGET_NETNSID:
    case IFLA_CARRIER_UP_COUNT:
    case IFLA_CARRIER_DOWN_COUNT:
    case IFLA_NEW_IFINDEX:
    case IFLA_MIN_MTU:
    case IFLA_MAX_MTU:
      break;
    default: fprintf(stderr, "Unknown RTA type %d len %d\n", rta->rta_type, rlen); return false;
  }
  return true;
}

static netstack_iface*
create_iface(void){
  netstack_iface* ni;
  ni = malloc(sizeof(*ni));
  memset(ni, 0, sizeof(*ni));
  return ni;
}

static void
free_iface(netstack_iface* ni){
  if(ni){
    free(ni);
  }
}

// FIXME disable cancellation in callbacks
static int
msg_handler(struct nl_msg* msg, void* vns){
  const netstack* ns = vns; (void)ns; // FIXME
  struct nlmsghdr* nhdr = nlmsg_hdr(msg);
  fprintf(stderr, "nl %db msg type %d\n", nhdr->nlmsg_len, nhdr->nlmsg_type);
  int nlen = nhdr->nlmsg_len;
  while(nlmsg_ok(nhdr, nlen)){
    const int ntype = nhdr->nlmsg_type;
    const struct rtattr *rta = NULL;
    const struct ifinfomsg* ifi = NLMSG_DATA(nhdr);
    netstack_iface* ni = create_iface();
    switch(ntype){
      case RTM_NEWLINK: rta = IFLA_RTA(ifi); break;
      default: fprintf(stderr, "Unknown nl type: %d\n", ntype); break;
    }
    if(rta == NULL){
      free_iface(ni);
      break;
    }
    // FIXME factor all of this out probably
    int rlen = nlen - NLMSG_LENGTH(sizeof(*ifi));
    while(RTA_OK(rta, rlen)){
      if(!link_rta_handler(ni, rta, rlen)){
        free_iface(ni);
        break;
      }
      rta = RTA_NEXT(rta, rlen);
    }
    if(rlen){
      free_iface(ni);
      fprintf(stderr, "Netlink attr was invalid, %db left\n", rlen);
      return NL_SKIP;
    }
    free_iface(ni); // FIXME do something with ni
    nhdr = nlmsg_next(nhdr, &nlen);
  }
  if(nlen){
    fprintf(stderr, "Netlink message was invalid, %db left\n", nlen);
    return NL_SKIP;
  }
  return NL_OK;
}

static int
err_handler(struct sockaddr_nl* nla, struct nlmsgerr* nlerr, void* vns){
  const netstack* ns = vns;
  fprintf(stderr, "Netlink error %p %p %p\n", nla, nlerr, ns);
  return NL_OK; // FIXME
}

// Get an initial dump of all entities. We'll receive updates via subscription.
static int
netstack_dump(netstack* ns){
  struct rtgenmsg rt = {
    .rtgen_family = AF_UNSPEC,
  };
  const int dumpmsgs[] = {
    RTM_GETLINK,
    RTM_GETADDR,
    /*RTM_GETROUTE,
    RTM_GETNEIGH,*/
  };
  size_t i;
  for(i = 0 ; i < sizeof(dumpmsgs) / sizeof(*dumpmsgs) ; ++i){
    if(nl_send_simple(ns->nl, dumpmsgs[i], NLM_F_REQUEST | NLM_F_DUMP, &rt, sizeof(rt)) < 0){
      return -1;
    }
  }
  return 0;
}

static int
netstack_init(netstack* ns){
  ns->txqueue[0] = -1;
  if((ns->nl = nl_socket_alloc()) == NULL){
    return -1;
  }
  nl_socket_disable_seq_check(ns->nl);
  if(nl_connect(ns->nl, NETLINK_ROUTE)){
    nl_socket_free(ns->nl);
    return -1;
  }
  if(nl_socket_add_memberships(ns->nl, RTNLGRP_LINK, NFNLGRP_NONE)){
    nl_socket_free(ns->nl);
    return -1;
  }
  // Passes this netstack object to libnl. The nl_sock thus must be destroyed
  // before the netstack itself is.
  if(nl_socket_modify_cb(ns->nl, NL_CB_VALID, NL_CB_CUSTOM, msg_handler, ns)){
    nl_socket_free(ns->nl);
    return -1;
  }
  if(nl_socket_modify_err_cb(ns->nl, NL_CB_CUSTOM, err_handler, ns)){
    nl_socket_free(ns->nl);
    return -1;
  }
  if(pthread_mutex_init(&ns->txlock, NULL)){
    nl_socket_free(ns->nl);
    return -1;
  }
  if(pthread_cond_init(&ns->txcond, NULL)){
    pthread_mutex_destroy(&ns->txlock);
    nl_socket_free(ns->nl);
    return -1;
  }
  if(pthread_create(&ns->rxtid, NULL, netstack_rx_thread, ns)){
    pthread_cond_destroy(&ns->txcond);
    pthread_mutex_destroy(&ns->txlock);
    nl_socket_free(ns->nl);
    return -1;
  }
  if(pthread_create(&ns->txtid, NULL, netstack_tx_thread, ns)){
    pthread_cancel(ns->rxtid) && pthread_join(ns->txtid, NULL);
    pthread_cond_destroy(&ns->txcond);
    pthread_mutex_destroy(&ns->txlock);
    nl_socket_free(ns->nl);
    return -1;
  }
  return 0;
}

netstack* netstack_create(const netstack_opts* nopts){
  if(nopts){
    if(nopts->no_thread){
      fprintf(stderr, "Threadless mode is not yet supported\n"); // FIXME
      return NULL;
    }
  }
  netstack* ns = malloc(sizeof(*ns));
  if(ns){
    if(netstack_init(ns)){
      free(ns);
      return NULL;
    }
    if(netstack_dump(ns)){
      netstack_destroy(ns);
      return NULL;
    }
  }
  return ns;
}

int netstack_destroy(netstack* ns){
  int ret = 0;
  if(ns){
    if(pthread_cancel(ns->rxtid) == 0 && pthread_cancel(ns->txtid) == 0){
      ret |= pthread_join(ns->txtid, NULL);
      ret |= pthread_join(ns->rxtid, NULL);
    }else{
      ret = -1;
    }
    ret |= pthread_cond_destroy(&ns->txcond);
    ret |= pthread_mutex_destroy(&ns->txlock);
    nl_socket_free(ns->nl);
    free(ns);
  }
  return ret;
}
