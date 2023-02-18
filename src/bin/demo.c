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
    .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK,
    .iface_cb = vnetstack_print_iface,
    .iface_curry = stdout,
    .addr_cb = vnetstack_print_addr,
    .addr_curry = stdout,
    .route_cb = vnetstack_print_route,
    .route_curry = stdout,
    .neigh_cb = vnetstack_print_neigh,
    .neigh_curry = stdout,
    .diagfxn = netstack_stderr_diag,
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
  struct timespec ts = { .tv_sec = 5, .tv_nsec = 0, };
  siginfo_t sinfo;
  while((sig = sigtimedwait(&sigset, &sinfo, &ts)) < 0){
    if(errno == EAGAIN){
      netstack_stats stats;
      netstack_sample_stats(ns, &stats);
      netstack_print_stats(&stats, stdout);
    }else{
      fprintf(stderr, "Couldn't wait on signals (%s)\n", strerror(errno));
      return EXIT_FAILURE;
    }
  }
  printf("Got signal %d, cleaning up...\n", sig);
  if(netstack_destroy(ns)){
    return EXIT_FAILURE;
  }
  printf("Done!\n");
  return EXIT_SUCCESS;
}
