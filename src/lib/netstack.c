#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include "netstack.h"

typedef struct netstack {
  int fd; // netlink fd
} netstack;

netstack* netstack_create(const netstack_opts* nopts){
  if(nopts){
    if(nopts->no_thread){
      fprintf(stderr, "Threadless mode is not yet supported\n"); // FIXME
      return NULL;
    }
  }
  netstack* ns = malloc(sizeof(*ns));
  if(ns){
    if((ns->fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE)) < 0){
      fprintf(stderr, "Error getting rtnetlink socket (%s)\n", strerror(errno));
      free(ns);
      return NULL;
    }
    // FIXME
  }
  return ns;
}

int netstack_destroy(netstack* ns){
  int ret = 0;
  if(close(ns->fd)){
    fprintf(stderr, "Error closing %d (%s)\n", ns->fd, strerror(errno));
  }
  free(ns);
  return ret;
}
