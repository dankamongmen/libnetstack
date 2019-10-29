#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <netlink/socket.h>
#include <netlink/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include "netstack.h"

typedef struct netstack {
  struct nl_sock* nl;  // netlink connection abstraction from libnl
  pthread_t tid;
  bool thread_spawned;
} netstack;

// Sits on blocking nl_recvmsgs()
static void*
netstack_thread(void* vns){
  netstack* ns = vns;
  while(nl_recvmsgs_default(ns->nl) == 0){
    printf("Got a netlink message!\n"); // FIXME
  }
  fprintf(stderr, "Error receiving from netlink socket (%s)\n",
          strerror(errno));
  // FIXME recover?
  return NULL;
}

static int
msg_handler(struct nl_msg* msg, void* vns){
  const netstack* ns = vns;
  fprintf(stderr, "Netlink msg %p %p\n", msg, ns);
  return NL_OK; // FIXME
}

static int
err_handler(struct sockaddr_nl* nla, struct nlmsgerr* nlerr, void* vns){
  const netstack* ns = vns;
  fprintf(stderr, "Netlink error %p %p %p\n", nla, nlerr, ns);
  return NL_OK; // FIXME
}

static int
netstack_init(netstack* ns){
  ns->thread_spawned = false;
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
  ns->thread_spawned = true;
  if(pthread_create(&ns->tid, NULL, netstack_thread, ns)){
    ns->thread_spawned = false;
    nl_socket_free(ns->nl);
    return -1;
  }
  return 0;
}

// Get an initial dump of all entities. We'll receive updates via subscription.
static int
netstack_dump(netstack* ns){
  struct rtgenmsg rt = {
    .rtgen_family = AF_UNSPEC,
  };
  const int dumpmsgs[] = {
    RTM_GETLINK,
    /*RTM_GETADDR,
    RTM_GETROUTE,
    RTM_GETNEIGH,*/
  };
  size_t i;
  for(i = 0 ; i < sizeof(dumpmsgs) / sizeof(*dumpmsgs) ; ++i){
    if(nl_send_simple(ns->nl, dumpmsgs[i], NLM_F_DUMP, &rt, sizeof(rt)) < 0){
      return -1;
    }
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
  if(ns->thread_spawned){
    void* vret;
    if(pthread_cancel(ns->tid) == 0){
      ret |= pthread_join(ns->tid, &vret);
    }else{
      ret = -1;
    }
  }
  nl_socket_free(ns->nl);
  free(ns);
  return ret;
}
