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
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
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
  return EXIT_SUCCESS;
}
