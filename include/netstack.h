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
struct netstack_addr;
struct netstack_neigh;
struct netstack_route;

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

// Takes a 1-biased offset, where 0 is understood to be an invalid offset (and
// will thus return NULL).
static inline const struct rtattr*
index_into_rta(const struct rtattr* rtabuf, size_t offset){
  if(offset-- == 0){
    return NULL;
  }
  return (const struct rtattr*)(((const char*)rtabuf) + offset);
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

const struct rtattr* netstack_neigh_attr(const struct netstack_neigh* nn, int attridx);
int netstack_neigh_index(const struct netstack_neigh* nn);
int netstack_neigh_family(const struct netstack_neigh* nn); // always AF_UNSPEC

const struct rtattr* netstack_addr_attr(const struct netstack_addr* na, int attridx);
int netstack_addr_family(const struct netstack_addr* na);
int netstack_addr_index(const struct netstack_addr* na);
int netstack_addr_prefixlen(const struct netstack_addr* na);

const struct rtattr* netstack_route_attr(const struct netstack_route* nr, int attridx);
// Routing tables are indexed 0-255
unsigned netstack_route_family(const struct netstack_route* nr);
unsigned netstack_route_table(const struct netstack_route* nr);
unsigned netstack_route_type(const struct netstack_route* nr);
unsigned netstack_route_proto(const struct netstack_route* nr);
unsigned netstack_route_dst_len(const struct netstack_route* nr);

static inline int
netstack_route_intattr(const struct netstack_route* nr, int attr){
  const struct rtattr* rt = netstack_route_attr(nr, attr);
  int ret = 0;
  if(rt && RTA_PAYLOAD(rt) == sizeof(ret)){
    memcpy(&ret, RTA_DATA(rt), RTA_PAYLOAD(rt));
  }
  return ret;
}

static inline int
netstack_route_iif(const struct netstack_route* nr){
  return netstack_route_intattr(nr, RTA_IIF);
}

static inline int
netstack_route_oif(const struct netstack_route* nr){
  return netstack_route_intattr(nr, RTA_OIF);
}

static inline int
netstack_route_priority(const struct netstack_route* nr){
  return netstack_route_intattr(nr, RTA_PRIORITY);
}

static inline int
netstack_route_metric(const struct netstack_route* nr){
  return netstack_route_intattr(nr, RTA_METRICS);
}

static inline const char*
netstack_route_typestr(unsigned rtype){
  switch(rtype){
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
netstack_route_protstr(unsigned proto){
  switch(proto){
    case RTPROT_UNSPEC: return "unknown";
    case RTPROT_REDIRECT: return "icmp";
    case RTPROT_KERNEL: return "kernel";
    case RTPROT_BOOT: return "boot";
    case RTPROT_STATIC: return "admin";
    case RTPROT_GATED: return "gated";
    case RTPROT_RA: return "rdisc/nd";
    case RTPROT_MRT: return "meritmrt";
    case RTPROT_ZEBRA: return "zebra";
    case RTPROT_BIRD: return "bird";
    case RTPROT_DNROUTED: return "decnet";
    case RTPROT_XORP: return "xdrp";
    case RTPROT_NTK: return "netsukuku";
    case RTPROT_DHCP: return "dhcp";
    case RTPROT_MROUTED: return "mcastd";
    case RTPROT_BABEL: return "babeld";
    case RTPROT_BGP: return "bgp";
    case RTPROT_ISIS: return "isis";
    case RTPROT_OSPF: return "ospf";
    case RTPROT_RIP: return "rip";
    case RTPROT_EIGRP: return "eigrp";
    default: return "?";
  }
}

typedef enum {
  NETSTACK_MOD, // a non-destructive event about an object
  NETSTACK_DEL, // an object that is going away
} netstack_event_e;

// Callback types for various events. Even though routes, addresses etc. can be
// reached through a netstack_iface, they each get their own type of callback.
typedef void (*netstack_iface_cb)(const struct netstack_iface*, netstack_event_e, void*);
typedef void (*netstack_addr_cb)(const struct netstack_addr*, netstack_event_e, void*);
typedef void (*netstack_route_cb)(const struct netstack_route*, netstack_event_e, void*);
typedef void (*netstack_neigh_cb)(const struct netstack_neigh*, netstack_event_e, void*);

// Policy for initial object dump. _ASYNC will cause events for existing
// objects, but netstack_create() may return before they've been received.
// _BLOCK blocks netstack_create() from returning until all initial enumeration
// events have been received. _NONE inhibits initial enumeration.
typedef enum {
  NETSTACK_INITIAL_EVENTS_ASYNC,
  NETSTACK_INITIAL_EVENTS_BLOCK,
  NETSTACK_INITIAL_EVENTS_NONE,
} netstack_initial_e;

// The default for all members is false or the appropriate zero representation.
// It is invalid to supply a non-NULL curry together with a NULL callback for
// any type. It is invalid to supply no callbacks together with all notracks.
typedef struct netstack_opts {
  // a given curry may be non-NULL only if the corresponding cb is also NULL.
  netstack_iface_cb iface_cb;
  void* iface_curry;
  netstack_addr_cb addr_cb;
  void* addr_curry;
  netstack_route_cb route_cb;
  void* route_curry;
  netstack_neigh_cb neigh_cb;
  void* neigh_curry;
  // If set, do not cache the corresponding type of object
  bool iface_notrack, addr_notrack, route_notrack, neigh_notrack;
  netstack_initial_e initial_events; // policy for initial object enumeration
} netstack_opts;

// Opts may be NULL, in which case the defaults will be used.
struct netstack* netstack_create(const netstack_opts* opts);
int netstack_destroy(struct netstack* ns);

// Count of interfaces in the active store. If iface_notrack is set, this will
// always return 0.
unsigned netstack_iface_count(const struct netstack* ns);

struct netstack_enumerator;

// If there is not sufficient room in the buffers to copy all of the objects in
// a single atomic operation, return -1 and perform as little work as possible.
#define NETSTACK_ENUMERATE_ATOMIC  0x0001
#define NETSTACK_ENUMERATE_MINIMAL 0x0002 // Copy only the most important data
// Abort the enumeration operation. streamer should be non-NULL. No other flags
// may be set in comvination with NETSTACK_ENUMERATE_ABORT.
#define NETSTACK_ENUMERATE_ABORT   0x0004

// Enumerate up to n netstack_ifaces via copy. offsets must have space for at
// least n elements, which will serve as offsets into objs. objs is a flat
// array of size obytes. flags is a bitfield composed of the NETSTACK_ENUMERATE
// constants.
int netstack_iface_enumerate(const uint32_t* offsets, void* objs,
                             size_t obytes, int n, unsigned flags,
                             struct netstack_enumerator** streamer);

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

// Copy/share a netstack_iface to which we already have a handle, for
// instance directly from the callback context. This is faster than the
// alternatives, as it needn't perform a lookup.
const struct netstack_iface* netstack_iface_share(const struct netstack_iface* ni);
struct netstack_iface* netstack_iface_copy(const struct netstack_iface* ni);

// Release a netstack_iface acquired from the netstack through either a copy or
// a share operation. Note that while the signature claims constness, ns will
// actually presumably be mutated (via alias). It is thus imperative that the
// passed object not be used again by the caller!
void netstack_iface_abandon(const struct netstack_iface* ni);

// Print human-readable object summaries to the specied FILE*. -1 on error.
int netstack_print_iface(const struct netstack_iface* ni, FILE* out);
int netstack_print_addr(const struct netstack_addr* na, FILE* out);
int netstack_print_route(const struct netstack_route* nr, FILE* out);
int netstack_print_neigh(const struct netstack_neigh* nn, FILE* out);

#ifdef __cplusplus
}
#else
// These wrappers have type signatures suitable for use as netstack callbacks.
// The curry must be a valid FILE*. The use of void* makes them unsuitable
// for C++, so they're only defined for C.
static inline void
vnetstack_print_iface(const struct netstack_iface* ni, netstack_event_e etype, void* vf){
  fputc(etype == NETSTACK_DEL ? '*' : ' ', vf);
  netstack_print_iface(ni, vf);
}

static inline void
vnetstack_print_addr(const struct netstack_addr* na, netstack_event_e etype, void* vf){
  fputc(etype == NETSTACK_DEL ? '*' : ' ', vf);
  netstack_print_addr(na, vf);
}

static inline void
vnetstack_print_route(const struct netstack_route* nr, netstack_event_e etype, void* vf){
  fputc(etype == NETSTACK_DEL ? '*' : ' ', vf);
  netstack_print_route(nr, vf);
}

static inline void
vnetstack_print_neigh(const struct netstack_neigh* nn, netstack_event_e etype, void* vf){
  fputc(etype == NETSTACK_DEL ? '*' : ' ', vf);
  netstack_print_neigh(nn, vf);
}
#endif

#endif
