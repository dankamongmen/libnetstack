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

// each of these types corresponds to a different rtnetlink message type. we
// copy the payload directly from the netlink message to rtabuf, but that form
// requires o(n) to get to any given attribute. we store a table of o(1)
// pointers into this buffer at rta_indexed. if we're running on a newer
// kernel, we might get an attribute larger than we're prepared to handle.
// that's fine; interested parties can still extract it using rtnetlink(3)
// macros. the convenience functions netstack_*_attr() are provided for this
// purpose: each will retrieve the value via lookup if less than the MAX
// against which we were compiled, and do an o(n) check otherwise.
typedef struct netstack_iface {
  struct ifinfomsg ifi;
  char name[IFNAMSIZ]; // NUL-terminated, safely processed from IFLA_NAME
  void* rtabuf;        // copied directly from message
  void* rta_indexed[__IFLA_MAX];
  // FIXME
} netstack_iface;

static inline void*
netstack_iface_attr(const netstack_iface* ni, unsigned attridx){
  if(attridx < sizeof(ni->rta_indexed) / sizeof(*ni->rta_indexed)){
    return ni->rta_indexed[attridx];
  }
  // FIXME do o(n) check
  return NULL;
}

// name must be at least IFNAMSIZ bytes, or better yet sizeof(ni->name). this
// has been validated as safe to copy into char[IFNAMSIZ] (i.e. you'll
// definitely get a NUL terminator), unlike netstack_iface_attr(IFLA_NAME).
static inline char*
netstack_iface_name(const netstack_iface* ni, char* name){
  return strcpy(name, ni->name);
}

typedef struct netstack_addr {
  struct ifaddrmsg ifa;
  void* rtabuf;        // copied directly from message
  void* rta_indexed[__IFA_MAX];
  // FIXME
} netstack_addr;

static inline void*
netstack_addr_attr(const netstack_addr* na, unsigned attridx){
  if(attridx < sizeof(na->rta_indexed) / sizeof(*na->rta_indexed)){
    return na->rta_indexed[attridx];
  }
  // FIXME do o(n) check
  return NULL;
}

typedef struct netstack_route {
  struct rtmsg rt;
  // FIXME
  void* rtabuf;        // copied directly from message
  void* rta_indexed[__RTA_MAX];
} netstack_route;

static inline void*
netstack_route_attr(const netstack_route* nr, unsigned attridx){
  if(attridx < sizeof(nr->rta_indexed) / sizeof(*nr->rta_indexed)){
    return nr->rta_indexed[attridx];
  }
  // FIXME do o(n) check
  return NULL;
}

typedef struct netstack_neigh {
  struct ndmsg nd;
  // FIXME
  void* rtabuf;        // copied directly from message
  void* rta_indexed[__NDA_MAX];
} netstack_neigh;

static inline void*
netstack_neigh_attr(const netstack_neigh* nn, unsigned attridx){
  if(attridx < sizeof(nn->rta_indexed) / sizeof(*nn->rta_indexed)){
    return nn->rta_indexed[attridx];
  }
  // FIXME do o(n) check
  return NULL;
}

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
