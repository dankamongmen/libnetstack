#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <netlink/socket.h>
#include <netlink/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include "netstack.h"

typedef struct netstack {
  struct nl_sock* nl;  // netlink connection abstraction from libnl
  struct nl_cb* cbset; // libnl callback set
} netstack;

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
  if((ns->cbset = nl_cb_alloc(NL_CB_VERBOSE)) == NULL){
    nl_socket_free(ns->nl);
    return -1;
  }
  // Passes this netstack object to libnl. The nl_sock thus must be destroyed
  // before the netstack itself is.
  if(nl_cb_set(ns->cbset, NL_CB_VALID, NL_CB_CUSTOM, msg_handler, ns)){
    nl_socket_free(ns->nl);
    return -1;
  }
  if(nl_cb_err(ns->cbset, NL_CB_VERBOSE, err_handler, ns)){
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
  }
  return ns;
}

int netstack_destroy(netstack* ns){
  int ret = 0;
  nl_socket_free(ns->nl);
  free(ns);
  return ret;
}
