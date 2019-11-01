#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <netstack.h>

int main(void){
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGTERM);
  sigaddset(&sigset, SIGINT);
  netstack_opts nopts = {
    .iface_cb = vnetstack_print_iface,
    .iface_curry = stdout,
    .addr_cb = vnetstack_print_addr,
    .addr_curry = stdout,
    .route_cb = vnetstack_print_route,
    .route_curry = stdout,
    .neigh_cb = vnetstack_print_neigh,
    .neigh_curry = stdout,
    .no_thread = false,
  };
  struct netstack* ns = netstack_create(&nopts);
  if(ns == NULL){
    return EXIT_FAILURE;
  }
  if(pthread_sigmask(SIG_BLOCK, &sigset, NULL)){
    fprintf(stderr, "Couldn't block signals (%s)\n", strerror(errno));
    return EXIT_FAILURE;
  }
  int sig;
  printf("Waiting on signal...\n");
  if(sigwait(&sigset, &sig)){
    fprintf(stderr, "Couldn't wait on signals (%s)\n", strerror(errno));
    return EXIT_FAILURE;
  }
  printf("Got signal %d, cleaning up...\n", sig);
  if(netstack_destroy(ns)){
    return EXIT_FAILURE;
  }
  printf("Done!\n");
  return EXIT_SUCCESS;
}
