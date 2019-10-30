#ifndef LIBNETSTACK_NETSTACK
#define LIBNETSTACK_NETSTACK

#include <net/if.h>
#include <stdbool.h>
#include <linux/rtnetlink.h>

#ifdef __cplusplus
extern "C" {
#endif

// Libnetstack provides an interface to the rtnetlink(7) functionality of the
// Linux kernel (though nothing keeps it from being ported to other operating
// systems), and an indexed state reflecting some network namespace (see
// clone(2)'s description of CLONE_NEWNET, and ip-netns(8)).
//
// Upon creation, it subscribes to various netlink events, and then dumps the
// rtnetlink tables. It thus maintains an accurate picture of the underlying
// reality. Libnetstack can call back to userspace upon changes, and userspace
// can query a netstack object about state.
//
// Libnetstack is wholly thread-safe. Any number of threads may call into a
// single netstack object at once, until it is destroyed.

struct netstack;

typedef struct netstack_iface {
  struct ifinfomsg ifi;
  char name[IFNAMSIZ];
  // FIXME
} netstack_iface;

typedef struct netstack_addr {
  struct ifaddrmsg ifa;
  // FIXME
} netstack_addr;

typedef struct netstack_route {
  struct rtmsg rt;
  // FIXME
} netstack_route;

typedef struct netstack_neigh {
  struct ndmsg nd;
  // FIXME
} netstack_neigh;

// Callback types for various events. Even though routes, addresses etc. can be
// reached through a netstack_iface object, they each get their own type of
// callback.
typedef void (*netstack_iface_cb)(const netstack_iface*, void*);
typedef void (*netstack_addr_cb)(const netstack_addr*, void*);
typedef void (*netstack_route_cb)(const netstack_route*, void*);
typedef void (*netstack_neigh_cb)(const netstack_neigh*, void*);

// The default for all members is false or the appropriate zero representation.
typedef struct netstack_opts {
  // refrain from launching a thread to handle netlink events in the
  // background. caller will need to handle nonblocking I/O.
  bool no_thread;
  // if a given callback is NULL, the default will be used (print to stdout).
  // a given curry may be non-NULL only if the corresponding cb is also NULL.
  netstack_iface_cb iface_cb;
  void* iface_curry;
  netstack_addr_cb addr_cb;
  void* addr_curry;
  netstack_route_cb route_cb;
  void* route_curry;
  netstack_neigh_cb neigh_cb;
  void* neigh_curry;
} netstack_opts;

// Opts may be NULL, in which case the defaults will be used.
struct netstack* netstack_create(const netstack_opts* opts);
int netstack_destroy(struct netstack* ns);

int netstack_print_iface(const netstack_iface* ni, FILE* out);
int netstack_print_addr(const netstack_addr* na, FILE* out);
int netstack_print_route(const netstack_route* nr, FILE* out);
int netstack_print_neigh(const netstack_neigh* nn, FILE* out);

#ifdef __cplusplus
}
#endif

#endif
