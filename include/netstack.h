#ifndef LIBNETSTACK_NETSTACK
#define LIBNETSTACK_NETSTACK

#include <stdio.h>
#include <net/if.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/rtnetlink.h>

#ifdef __cplusplus
// see http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0943r3.html
#include <atomic>
#define _Atomic(T) std::atomic<T>
using std::atomic_int;
extern "C" {
#else
#include <stdatomic.h>
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
struct netstack_iface;

static inline const struct rtattr*
netstack_extract_rta_attr(const struct rtattr* rtabuf, size_t rlen, int rtype){
  while(RTA_OK(rtabuf, rlen)){
    if(rtabuf->rta_type == rtype){
      return rtabuf;
    }
    rtabuf = RTA_NEXT(rtabuf, rlen);
  }
  return NULL;
}

static inline const struct rtattr*
index_into_rta(const struct rtattr* rtabuf, size_t offset){
  return (const struct rtattr*)(((char*)rtabuf) + offset);
}

const struct rtattr* netstack_iface_attr(const struct netstack_iface* ni, int attridx);
char* netstack_iface_name(const struct netstack_iface* ni, char* name);
int netstack_iface_type(const struct netstack_iface* ni);
int netstack_iface_family(const struct netstack_iface* ni);
int netstack_iface_index(const struct netstack_iface* ni);

// pass in the maximum number of bytes available for copying the link-layer
// address. if this is sufficient, the actual number of bytes copied will be
// stored to this variable. otherwise, NULL will be returned.
static inline void*
netstack_iface_lladdr(const struct netstack_iface* ni, void* buf, size_t* len){
  const struct rtattr* rta = netstack_iface_attr(ni, IFLA_ADDRESS);
  if(rta == NULL || *len < RTA_PAYLOAD(rta)){
    return NULL;
  }
  memcpy(buf, RTA_DATA(rta), RTA_PAYLOAD(rta));
  return buf;
}

static inline uint32_t
netstack_iface_mtu(const struct netstack_iface* ni){
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
  size_t rta_index[__IFA_MAX];
  bool unknown_attrs;  // are there attrs >= __IFA_MAX?
} netstack_addr;

static inline const struct rtattr*
netstack_addr_attr(const netstack_addr* na, int attridx){
  if(attridx < 0){
    return NULL;
  }
  if((size_t)attridx < sizeof(na->rta_index) / sizeof(*na->rta_index)){
    return index_into_rta(na->rtabuf, na->rta_index[attridx]);
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
  size_t rta_index[__RTA_MAX];
  bool unknown_attrs;  // are there attrs >= __RTA_MAX?
} netstack_route;

static inline const struct rtattr*
netstack_route_attr(const netstack_route* nr, int attridx){
  if(attridx < 0){
    return NULL;
  }
  if((size_t)attridx < sizeof(nr->rta_index) / sizeof(*nr->rta_index)){
    return index_into_rta(nr->rtabuf, nr->rta_index[attridx]);
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
  size_t rta_index[__NDA_MAX];
  bool unknown_attrs;  // are there attrs >= __NDA_MAX?
} netstack_neigh;

static inline const struct rtattr*
netstack_neigh_attr(const netstack_neigh* nn, int attridx){
  if(attridx < 0){
    return NULL;
  }
  if((size_t)attridx < sizeof(nn->rta_index) / sizeof(*nn->rta_index)){
    return index_into_rta(nn->rtabuf, nn->rta_index[attridx]);
  }
  if(!nn->unknown_attrs){
    return NULL;
  }
  return netstack_extract_rta_attr(nn->rtabuf, nn->rtabuflen, attridx);
}

typedef enum {
  NETSTACK_MOD, // a non-destructive event about an object
  NETSTACK_DEL, // an object that is going away
} netstack_event_e;

// Callback types for various events. Even though routes, addresses etc. can be
// reached through a netstack_iface object, they each get their own type of
// callback.
typedef void (*netstack_iface_cb)(const struct netstack_iface*, netstack_event_e, void*);
typedef void (*netstack_addr_cb)(const netstack_addr*, netstack_event_e, void*);
typedef void (*netstack_route_cb)(const netstack_route*, netstack_event_e, void*);
typedef void (*netstack_neigh_cb)(const netstack_neigh*, netstack_event_e, void*);

// The default for all members is false or the appropriate zero representation.
typedef struct netstack_opts {
  // refrain from launching a thread to handle netlink events in the
  // background. caller will need to handle nonblocking I/O.
  bool no_thread;
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

// Take a reference on some netstack iface for read-only use in the client.
// There is no copy, but the object still needs to be freed by a call to
// netstack_iface_abandon().
const struct netstack_iface* netstack_iface_share_byname(struct netstack* ns, const char* name);
const struct netstack_iface* netstack_iface_share_byidx(struct netstack* ns, int idx);

// Copy out a netstack iface for arbitrary use in the client. This is a
// heavyweight copy, and must be freed using netstack_iface_destroy(). You
// would usually be better served by netstack_iface_share_*().
struct netstack_iface* netstack_iface_copy_byname(struct netstack* ns, const char* name);
struct netstack_iface* netstack_iface_copy_byidx(struct netstack* ns, int idx);

// Release a netstack_iface acquired from the netstack through either a copy or
// a share operation. Note that while the signature claims constness, ns will
// actually presumably be mutated (via alias). It is thus imperative that the
// passed object not be used again by the caller!
void netstack_iface_abandon(const struct netstack_iface* ns);

// Print human-readable object summaries to the specied FILE*. -1 on error.
int netstack_print_iface(const struct netstack_iface* ni, FILE* out);
int netstack_print_addr(const netstack_addr* na, FILE* out);
int netstack_print_route(const netstack_route* nr, FILE* out);
int netstack_print_neigh(const netstack_neigh* nn, FILE* out);

static inline const char*
netstack_event_str(netstack_event_e etype){
  switch(etype){
    case NETSTACK_MOD: return "mod";
    case NETSTACK_DEL: return "del";
    default: return "???";
  }
}

// These wrappers have type signatures suitable for use as netstack callbacks.
// The curry must be a valid FILE*.
static inline void
vnetstack_print_iface(const struct netstack_iface* ni, netstack_event_e etype, void* vf){
  FILE* f = (FILE*)vf;
  fprintf(f, "%s ", netstack_event_str(etype));
  netstack_print_iface(ni, f);
}

static inline void
vnetstack_print_addr(const netstack_addr* na, netstack_event_e etype, void* vf){
  FILE* f = (FILE*)vf;
  fprintf(f, "%s ", netstack_event_str(etype));
  netstack_print_addr(na, f);
}

static inline void
vnetstack_print_route(const netstack_route* nr, netstack_event_e etype, void* vf){
  FILE* f = (FILE*)vf;
  fprintf(f, "%s ", netstack_event_str(etype));
  netstack_print_route(nr, f);
}

static inline void
vnetstack_print_neigh(const netstack_neigh* nn, netstack_event_e etype, void* vf){
  FILE* f = (FILE*)vf;
  fprintf(f, "%s ", netstack_event_str(etype));
  netstack_print_neigh(nn, f);
}

#ifdef __cplusplus
}
#endif

#endif
