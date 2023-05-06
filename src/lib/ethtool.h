#ifndef LIBNETSTACK_SRC_LIB_ETHTOOL
#define LIBNETSTACK_SRC_LIB_ETHTOOL

struct nl_sock* ethtool_socket_connect(const struct netstack_opts* opts);

#endif
