#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "netstack.h"

typedef struct netstack {
  int fd; // netlink fd
} netstack;

netstack* netstack_create(const netstack_opts* nopts){
  if(nopts->no_thread){
    fprintf(stderr, "Threadless mode is not yet supported\n"); // FIXME
    return NULL;
  }
  netstack* ns = malloc(sizeof(*ns));
  if(ns){
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
