#ifndef LIBNETSTACK_NETSTACK
#define LIBNETSTACK_NETSTACK

#include <net/if.h>
#include <stdbool.h>

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

// The default for all members is false or the appropriate zero representation.
typedef struct netstack_opts {
  // refrain from launching a thread to handle netlink events in the
  // background. caller will need to handle nonblocking I/O.
  bool no_thread;
} netstack_opts;

// Opts may be NULL, in which case the defaults will be used.
struct netstack* netstack_create(const netstack_opts* opts);
int netstack_destroy(struct netstack* ns);

typedef struct netstack_dev {
  char name[IFNAMSIZ];
} netstack_dev;

// Callback type for netstack_foreach_dev().
typedef int(*netstackcb)(const netstack_dev*, void*);

int netstack_foreach_dev(struct netstack* ns, void* curry);

#ifdef __cplusplus
}
#endif

#endif
