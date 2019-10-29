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
  struct nl_sock* nl; // netlink connection abstraction from libnl
} netstack;

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
