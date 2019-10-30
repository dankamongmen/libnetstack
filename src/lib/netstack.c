#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <netlink/msg.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <netlink/socket.h>
#include <netlink/netlink.h>
#include <linux/rtnetlink.h>
#include "netstack.h"

typedef struct netstack {
  struct nl_sock* nl;  // netlink connection abstraction from libnl
  pthread_t rxtid;
  pthread_t txtid;
  // We can only have one command of the class e.g. DUMP outstanding at a time.
  // Queue up any others for transmission when possible.
  // Iff txqueue[dequeueidx] == -1, there are no messages to send.
  // Iff txqueue[queueidx] >= 0, there is no room to enqueue messages.
  int txqueue[128];
  int dequeueidx, queueidx;
  // Unset by the txthread, set by the rxthread (and initializer).
  pthread_cond_t txcond;
  pthread_mutex_t txlock;
  atomic_bool clear_to_send;
  netstack_opts opts; // copied wholesale in netstack_create()
} netstack;

// Sits on blocking nl_recvmsgs()
static void*
netstack_rx_thread(void* vns){
  netstack* ns = vns;
  int ret;
  while((ret = nl_recvmsgs_default(ns->nl)) == 0){
    printf("Got a netlink message!\n"); // FIXME
    // FIXME ensure it matched what we expect?
    ns->clear_to_send = true;
    pthread_cond_signal(&ns->txcond);
  }
  fprintf(stderr, "Error rxing from netlink socket (%s)\n", nl_geterror(ret));
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
  while(true){
    pthread_mutex_lock(&ns->txlock);
    pthread_cleanup_push(tx_cancel_clean, ns);
    while(!ns->clear_to_send || ns->txqueue[ns->dequeueidx] == -1){
      pthread_cond_wait(&ns->txcond, &ns->txlock);
    }
    ns->clear_to_send = false;
    struct rtgenmsg rt = {
      .rtgen_family = AF_UNSPEC,
    };
    if(nl_send_simple(ns->nl, ns->txqueue[ns->dequeueidx],
                      NLM_F_REQUEST|NLM_F_DUMP, &rt, sizeof(rt)) < 0){
      // FIXME do what?
    }
    ns->txqueue[ns->dequeueidx++] = -1;
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&ns->txlock);
  }
  return NULL;
}

static bool
addr_rta_handler(netstack_addr* na, const struct ifaddrmsg* ifa,
                 const struct rtattr* rta, int* rlen){
  memcpy(&na->ifa, ifa, sizeof(*ifa));
  // FIXME
  return true;
}

static bool
route_rta_handler(netstack_route* nr, const struct rtmsg* rt,
                  const struct rtattr* rta, int* rlen){
  memcpy(&nr->rt, rt, sizeof(*rt));
  // FIXME
  return true;
}

static bool
neigh_rta_handler(netstack_neigh* nn, const struct ndmsg* nd,
                  const struct rtattr* rta, int* rlen){
  memcpy(&nn->nd, nd, sizeof(*nd));
  // FIXME
  return true;
}

static bool
link_rta_handler(netstack_iface* ni, const struct ifinfomsg* ifi,
                 const struct rtattr* rta, int* rlen){
  memcpy(&ni->ifi, ifi, sizeof(*ifi));
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
    default: fprintf(stderr, "Unknown RTA type %d len %d\n", rta->rta_type, *rlen); return false;
  }
  return true;
}

// FIXME xmacro all of these out
static bool
vlink_rta_handler(void* v1, const void* v2, const struct rtattr* rta, int* rlen){
  return link_rta_handler(v1, v2, rta, rlen);
}

static bool
vaddr_rta_handler(void* v1, const void* v2, const struct rtattr* rta, int* rlen){
  return addr_rta_handler(v1, v2, rta, rlen);
}

static bool
vroute_rta_handler(void* v1, const void* v2, const struct rtattr* rta, int* rlen){
  return route_rta_handler(v1, v2, rta, rlen);
}

static bool
vneigh_rta_handler(void* v1, const void* v2, const struct rtattr* rta, int* rlen){
  return neigh_rta_handler(v1, v2, rta, rlen);
}

static netstack_iface*
create_iface(void){
  netstack_iface* ni;
  ni = malloc(sizeof(*ni));
  memset(ni, 0, sizeof(*ni));
  return ni;
}

static netstack_addr*
create_addr(void){
  netstack_addr* na;
  na = malloc(sizeof(*na));
  memset(na, 0, sizeof(*na));
  return na;
}

static netstack_route*
create_route(void){
  netstack_route* nr;
  nr = malloc(sizeof(*nr));
  memset(nr, 0, sizeof(*nr));
  return nr;
}

static netstack_neigh*
create_neigh(void){
  netstack_neigh* nn;
  nn = malloc(sizeof(*nn));
  memset(nn, 0, sizeof(*nn));
  return nn;
}

static void
free_iface(netstack_iface* ni){
  if(ni){
    free(ni);
  }
}

static void
free_addr(netstack_addr* na){
  if(na){
    free(na);
  }
}

static void
free_route(netstack_route* nr){
  if(nr){
    free(nr);
  }
}

static void
free_neigh(netstack_neigh* nn){
  if(nn){
    free(nn);
  }
}

static void vfree_iface(void* vni){ free_iface(vni); }
static void vfree_addr(void* va){ free_addr(va); }
static void vfree_route(void* vr){ free_route(vr); }
static void vfree_neigh(void* vn){ free_neigh(vn); }

#ifndef NDA_RTA
#define NDA_RTA(r) \
 ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ndmsg))))
#endif
#ifndef NDA_PAYLOAD
#define NDA_PAYLOAD(n) \
 NLMSG_PAYLOAD((n), sizeof(struct ndmsg))
#endif

static int
msg_handler_internal(struct nl_msg* msg, const netstack* ns){
  struct nlmsghdr* nhdr = nlmsg_hdr(msg);
  int nlen = nhdr->nlmsg_len;
  while(nlmsg_ok(nhdr, nlen)){
    const int ntype = nhdr->nlmsg_type;
    const struct rtattr *rta = NULL;
    const struct ifinfomsg* ifi = NLMSG_DATA(nhdr);
    const struct ifaddrmsg* ifa = NLMSG_DATA(nhdr);
    const struct rtmsg* rt = NLMSG_DATA(nhdr);
    const struct ndmsg* nd = NLMSG_DATA(nhdr);
    const void* hdr = NULL; // aliases one of the NLMSG_DATA lvalues above
    size_t hdrsize = 0; // size of leading object (hdr), depends on message type
    void* newobj = NULL; // type depends on message type, result of create_*()
    // processor for the type. takes the new netstack_* object (newobj), the
    // leading type-dependent object (aliased by hdr), the first RTA, and &rlen.
    bool (*pfxn)(void*, const void*, const struct rtattr*, int*) = NULL;
    void (*dfxn)(void*) = NULL; // destroyer of this type of object
    switch(ntype){
      case RTM_NEWLINK:
        hdr = ifi;
        rta = IFLA_RTA(ifi);
        hdrsize = sizeof(*ifi);
        pfxn = vlink_rta_handler;
        dfxn = vfree_iface;
        newobj = create_iface();
        break;
      case RTM_NEWADDR:
        hdr = ifa;
        rta = IFA_RTA(ifa);
        hdrsize = sizeof(*ifa);
        pfxn = vaddr_rta_handler;
        dfxn = vfree_addr;
        newobj = create_addr();
        break;
      case RTM_NEWROUTE:
        hdr = rt;
        rta = RTM_RTA(rt);
        hdrsize = sizeof(*rt);
        pfxn = vroute_rta_handler;
        dfxn = vfree_route;
        newobj = create_route();
        break;
      case RTM_NEWNEIGH:
        hdr = nd;
        rta = NDA_RTA(nd);
        hdrsize = sizeof(*nd);
        pfxn = vneigh_rta_handler;
        dfxn = vfree_neigh;
        newobj = create_neigh();
        break;
      default: fprintf(stderr, "Unknown nl type: %d\n", ntype); break;
    }
    if(newobj == NULL){
      break;
    }
    // FIXME factor all of this out probably
    int rlen = nlen - NLMSG_LENGTH(hdrsize);
    while(RTA_OK(rta, rlen)){
      if(!pfxn(newobj, hdr, rta, &rlen)){
        dfxn(newobj);
        break;
      }
      rta = RTA_NEXT(rta, rlen);
    }
    if(rlen){
      dfxn(newobj);
      fprintf(stderr, "Netlink attr was invalid, %db left\n", rlen);
      return NL_SKIP;
    }
    dfxn(newobj); // FIXME do something with newobj
    nhdr = nlmsg_next(nhdr, &nlen);
  }
  if(nlen){
    fprintf(stderr, "Netlink message was invalid, %db left\n", nlen);
    return NL_SKIP;
  }
  return NL_OK;
}

static int
msg_handler(struct nl_msg* msg, void* vns){
  int oldcancelstate, ret;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldcancelstate);
  ret = msg_handler_internal(msg, vns);
  pthread_setcancelstate(oldcancelstate, &oldcancelstate);
  return ret;
}

static int
err_handler(struct sockaddr_nl* nla, struct nlmsgerr* nlerr, void* vns){
  const netstack* ns = vns;
  fprintf(stderr, "Netlink error %p %p %p\n", nla, nlerr, ns);
  return NL_OK; // FIXME
}

static int
netstack_init(netstack* ns, const netstack_opts* opts){
  // Get an initial dump of all entities, then updates via subscription.
  static const int dumpmsgs[] = {
    RTM_GETLINK,
    RTM_GETADDR,
    RTM_GETROUTE,
    RTM_GETNEIGH,
  };
  memcpy(ns->txqueue, dumpmsgs, sizeof(dumpmsgs));
  ns->txqueue[sizeof(dumpmsgs) / sizeof(*dumpmsgs)] = -1;
  ns->queueidx = sizeof(dumpmsgs) / sizeof(*dumpmsgs);
  ns->dequeueidx = 0;
  ns->clear_to_send = true;
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
  memcpy(&ns->opts, opts, sizeof(*opts));
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
    if(netstack_init(ns, nopts)){
      free(ns);
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

int netstack_print_iface(const netstack_iface* ni, FILE* out){
  int ret = 0;
  ret = fprintf(out, "[%s]\n", ni->name); // FIXME
  return ret;
}
