#include <stdlib.h>
#include <netstack.h>

int main(void){
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  struct netstack* ns = netstack_create(&nopts);
  if(ns == NULL){
    return EXIT_FAILURE;
  }
  // FIXME
  if(netstack_destroy(ns)){
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
