#ifndef LIBNETSTACK_NETSTACK
#define LIBNETSTACK_NETSTACK

#include <net/if.h>
#include <stdint.h>
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
  struct rtattr* rtabuf; // copied directly from message
  size_t rtabuflen; // number of bytes copied to rtabuf
  // set up before invoking the user callback, these allow for o(1) index into
  // rtabuf by attr type. NULL if that attr wasn't in the message.
  const struct rtattr* rta_indexed[__IFLA_MAX];
  bool unknown_attrs; // are there attrs >= __IFLA_MAX?
} netstack_iface;

static inline const struct rtattr *
netstack_extract_rta_attr(const struct rtattr* rtabuf, size_t rlen, int rtype){
  while(RTA_OK(rtabuf, rlen)){
    if(rtabuf->rta_type == rtype){
      return rtabuf;
    }
    rtabuf = RTA_NEXT(rtabuf, rlen);
  }
  return NULL;
}

static inline const struct rtattr *
netstack_iface_attr(const netstack_iface* ni, int attridx){
  if(attridx < 0){
    return NULL;
  }
  if((size_t)attridx < sizeof(ni->rta_indexed) / sizeof(*ni->rta_indexed)){
    return ni->rta_indexed[attridx];
  }
  if(!ni->unknown_attrs){
    return NULL;
  }
  return netstack_extract_rta_attr(ni->rtabuf, ni->rtabuflen, attridx);
}

// name must be at least IFNAMSIZ bytes, or better yet sizeof(ni->name). this
// has been validated as safe to copy into char[IFNAMSIZ] (i.e. you'll
// definitely get a NUL terminator), unlike netstack_iface_attr(IFLA_NAME).
static inline char*
netstack_iface_name(const netstack_iface* ni, char* name){
  return strcpy(name, ni->name);
}

// pass in the maximum number of bytes available for copying the link-layer
// address. if this is sufficient, the actual number of bytes copied will be
// stored to this variable. otherwise, NULL will be returned.
static inline void*
netstack_iface_lladdr(const netstack_iface* ni, void* buf, size_t* len){
  const struct rtattr* rta = netstack_iface_attr(ni, IFLA_ADDRESS);
  if(rta == NULL || *len < RTA_PAYLOAD(rta)){
    return NULL;
  }
  memcpy(buf, RTA_DATA(rta), RTA_PAYLOAD(rta));
  return buf;
}

static inline uint32_t
netstack_iface_mtu(const netstack_iface* ni){
  const struct rtattr* attr = netstack_iface_attr(ni, IFLA_MTU);
  uint32_t ret;
  if(attr){
    if(RTA_PAYLOAD(attr) != sizeof(ret)){
      return 0;
    }
    memcpy(&ret, RTA_DATA(attr), RTA_PAYLOAD(attr));
  }else{
    ret = 0;
  }
  return ret;
}

typedef struct netstack_addr {
  struct ifaddrmsg ifa;
  struct rtattr* rtabuf;        // copied directly from message
  size_t rtabuflen;
  const struct rtattr* rta_indexed[__IFA_MAX];
  bool unknown_attrs;  // are there attrs >= __IFA_MAX?
} netstack_addr;

static inline const struct rtattr*
netstack_addr_attr(const netstack_addr* na, int attridx){
  if(attridx < 0){
    return NULL;
  }
  if((size_t)attridx < sizeof(na->rta_indexed) / sizeof(*na->rta_indexed)){
    return na->rta_indexed[attridx];
  }
  if(!na->unknown_attrs){
    return NULL;
  }
  return netstack_extract_rta_attr(na->rtabuf, na->rtabuflen, attridx);
}

typedef struct netstack_route {
  struct rtmsg rt;
  struct rtattr* rtabuf;        // copied directly from message
  size_t rtabuflen;
  const struct rtattr* rta_indexed[__RTA_MAX];
  bool unknown_attrs;  // are there attrs >= __RTA_MAX?
} netstack_route;

static inline const struct rtattr*
netstack_route_attr(const netstack_route* nr, int attridx){
  if(attridx < 0){
    return NULL;
  }
  if((size_t)attridx < sizeof(nr->rta_indexed) / sizeof(*nr->rta_indexed)){
    return nr->rta_indexed[attridx];
  }
  if(!nr->unknown_attrs){
    return NULL;
  }
  return netstack_extract_rta_attr(nr->rtabuf, nr->rtabuflen, attridx);
}

// Routing tables are indexed 0-255
static inline unsigned
netstack_route_table(const netstack_route* nr){
  return nr->rt.rtm_table;
}

static inline const char*
netstack_route_typestr(const netstack_route* nr){
  switch(nr->rt.rtm_type){
    case RTN_UNSPEC: return "none";
    case RTN_UNICAST: return "unicast";
    case RTN_LOCAL: return "local";
    case RTN_BROADCAST: return "broadcast";
    case RTN_ANYCAST: return "anycast";
    case RTN_MULTICAST: return "multicast";
    case RTN_BLACKHOLE: return "blackhole";
    case RTN_UNREACHABLE: return "unreachable";
    case RTN_PROHIBIT: return "prohibit";
    case RTN_THROW: return "throw";
    case RTN_NAT: return "nat";
    case RTN_XRESOLVE: return "xresolve";
    default: return "";
  }
}

static inline const char*
netstack_route_protstr(const netstack_route* nr){
  switch(nr->rt.rtm_protocol){
    case RTPROT_UNSPEC: return "unknown";
    case RTPROT_REDIRECT: return "icmp";
    case RTPROT_KERNEL: return "kernel";
    case RTPROT_BOOT: return "boot";
    case RTPROT_STATIC: return "admin";
    default: return "";
  }
}

static inline int
netstack_route_intattr(const netstack_route* nr, int attr){
  const struct rtattr* rt = netstack_route_attr(nr, attr);
  int ret = 0;
  if(rt && RTA_PAYLOAD(rt) == sizeof(ret)){
    memcpy(&ret, RTA_DATA(rt), RTA_PAYLOAD(rt));
  }
  return ret;
}


static inline int
netstack_route_iif(const netstack_route* nr){
  return netstack_route_intattr(nr, RTA_IIF);
}

static inline int
netstack_route_oif(const netstack_route* nr){
  return netstack_route_intattr(nr, RTA_OIF);
}

static inline int
netstack_route_priority(const netstack_route* nr){
  return netstack_route_intattr(nr, RTA_PRIORITY);
}

static inline int
netstack_route_metric(const netstack_route* nr){
  return netstack_route_intattr(nr, RTA_METRICS);
}

typedef struct netstack_neigh {
  struct ndmsg nd;
  struct rtattr* rtabuf;        // copied directly from message
  size_t rtabuflen;
  const struct rtattr* rta_indexed[__NDA_MAX];
  bool unknown_attrs;  // are there attrs >= __NDA_MAX?
} netstack_neigh;

static inline const struct rtattr*
netstack_neigh_attr(const netstack_neigh* nn, int attridx){
  if(attridx < 0){
    return NULL;
  }
  if((size_t)attridx < sizeof(nn->rta_indexed) / sizeof(*nn->rta_indexed)){
    return nn->rta_indexed[attridx];
  }
  if(!nn->unknown_attrs){
    return NULL;
  }
  return netstack_extract_rta_attr(nn->rtabuf, nn->rtabuflen, attridx);
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
