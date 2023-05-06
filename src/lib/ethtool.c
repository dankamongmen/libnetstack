// ethtool, once merely a set of ioctl()s, has been made available over
// NETLINK_GENERIC-family netlink sockets:
// https://www.kernel.org/doc/html/latest/networking/ethtool-netlink.html

#include <netlink/msg.h>
#include "netstack.h"
#include "ethtool.h"

struct nl_sock*
ethtool_socket_connect(const netstack_opts* opts __attribute__ ((unused))){
  struct nl_sock* nls = nl_socket_alloc();
  if(nls == NULL){
    return NULL;
  }
  if(nl_connect(nls, NETLINK_GENERIC)){
    nl_socket_free(nls);
    return NULL;
  }
  return nls;
}
