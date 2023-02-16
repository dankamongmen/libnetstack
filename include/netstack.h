#ifndef LIBNETSTACK_NETSTACK
#define LIBNETSTACK_NETSTACK

#include <stdio.h>
//#include <net/if.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/if.h>
#include <arpa/inet.h>
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
// systems with similar capabilities), and an indexed state reflecting some
// network namespace (see clone(2)'s CLONE_NEWNET, and ip-netns(8)).
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

typedef struct netstack_stats {
  // Current counts of each object class
  unsigned ifaces, addrs, routes, neighs;
  // Events for each object class (dumps + creations + changes + deletions)
  uintmax_t iface_events, addr_events, route_events, neigh_events;
  // The number of times a lookup + share or lookup + copy succeeded
  uintmax_t lookup_shares, lookup_copies;
  // Number of shares which have been invalidated but not destroyed
  uintmax_t zombie_shares;
  // The number of times the user looked up a key and it didn't exist
  uintmax_t lookup_failures;
  uintmax_t netlink_errors; // number of nlmsgerrs received from netlink
  uintmax_t user_callbacks_total; // number of times we've called back
} netstack_stats;

// Acquire the current statistics (might not be atomic)
netstack_stats* netstack_sample_stats(const struct netstack* ns,
                                      netstack_stats* stats);

// Objects arrive from netlink as a class-specific structure followed by a flat
// set of struct rtattr* TLVs. These functions deal with struct rtattrs and
// blocks thereof, and are primarily used by libnetstack itself.

// Walk a rlen-byte block of rtattrs, looking for the specified type.
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

// Bitwise copy of an RTA's payload into the provided user buf, assuming it is
// sufficiently large. Returns true on success, and sets *len to the number
// of bytes actually copied. Returns false if there is not enough room.
static inline bool
netstack_rtattrcpy(const struct rtattr* rta, void* buf, size_t* len){
  if(rta == NULL || *len < RTA_PAYLOAD(rta)){
    return false;
  }
  memcpy(buf, RTA_DATA(rta), RTA_PAYLOAD(rta));
  *len = RTA_PAYLOAD(rta);
  return true;
}

// Same deal as netstack_rtattrcpy(), but only performs the copy (and returns
// true) if the sizes are exactly equal.
static inline bool
netstack_rtattrcpy_exact(const struct rtattr* rta, void* buf, size_t len){
  if(rta == NULL || len != RTA_PAYLOAD(rta)){
    return false;
  }
  memcpy(buf, RTA_DATA(rta), RTA_PAYLOAD(rta));
  return true;
}

// Provided a link-layer type, an address of that type, and the correct length
// of that address, return a NUL-terminated, heap-allocated presentation-format
// version of that link-layer address.
char* netstack_l2addrstr(unsigned l2type, size_t len, const void* addr);

// Functions for inspecting netstack_ifaces
const struct rtattr* netstack_iface_attr(const struct netstack_iface* ni, int attridx);

// name must be at least IFNAMSIZ bytes. returns NULL if no name was reported,
// or the name was greater than IFNAMSIZ-1 bytes (should never happen).
char* netstack_iface_name(const struct netstack_iface* ni, char* name);
unsigned netstack_iface_type(const struct netstack_iface* ni);
char* netstack_iface_typestr(const struct netstack_iface* ni, char* buf, size_t blen);
unsigned netstack_iface_family(const struct netstack_iface* ni);
int netstack_iface_index(const struct netstack_iface* ni);
unsigned netstack_iface_flags(const struct netstack_iface* ni);

static inline bool netstack_iface_up(const struct netstack_iface* ni){
  return netstack_iface_flags(ni) & IFF_UP;
}

// Has a valid broadcast address been configured?
static inline bool netstack_iface_broadcast(const struct netstack_iface* ni){
  return netstack_iface_flags(ni) & IFF_BROADCAST;
}

// Is this a loopback device?
static inline bool netstack_iface_loopback(const struct netstack_iface* ni){
  return netstack_iface_flags(ni) & IFF_LOOPBACK;
}

// Is this a point-to-point link?
static inline bool netstack_iface_pointtopoint(const struct netstack_iface* ni){
  return netstack_iface_flags(ni) & IFF_POINTOPOINT;
}

// Does this link lack ARP?
static inline bool netstack_iface_noarp(const struct netstack_iface* ni){
  return netstack_iface_flags(ni) & IFF_NOARP;
}

// Is the interface in promiscuious mode?
static inline bool netstack_iface_promisc(const struct netstack_iface* ni){
  return netstack_iface_flags(ni) & IFF_PROMISC;
}

// pass in the maximum number of bytes available for copying the link-layer
// address. if this is sufficient, the actual number of bytes copied will be
// stored to this variable. otherwise, NULL will be returned.
static inline void*
netstack_iface_l2addr(const struct netstack_iface* ni, void* buf, size_t* len){
  const struct rtattr* rta = netstack_iface_attr(ni, IFLA_ADDRESS);
  return netstack_rtattrcpy(rta, buf, len) ? buf : NULL;
}

// Returns true iff there is an IFLA_ADDRESS layer 2 address associated with
// this entry, *and* it can be transformed into a presentable string.
// family will hold the result of netstack_iface_family(), even on error.
static inline char*
netstack_iface_addressstr(const struct netstack_iface* ni, unsigned* type){
  *type = netstack_iface_type(ni);
  const struct rtattr* rta = netstack_iface_attr(ni, IFLA_ADDRESS);
  if(rta == NULL){
    return NULL;
  }
  return netstack_l2addrstr(*type, RTA_PAYLOAD(rta), RTA_DATA(rta));
}

// same deal as netstack_iface_l2addr(), but for the broadcast link-layer
// address (if one exists).
static inline void*
netstack_iface_l2broadcast(const struct netstack_iface* ni, void* buf, size_t* len){
  const struct rtattr* rta = netstack_iface_attr(ni, IFLA_BROADCAST);
  return netstack_rtattrcpy(rta, buf, len) ? buf : NULL;
}

static inline char*
netstack_iface_broadcaststr(const struct netstack_iface* ni, unsigned* type){
  *type = netstack_iface_type(ni);
  const struct rtattr* rta = netstack_iface_attr(ni, IFLA_BROADCAST);
  if(rta == NULL){
    return NULL;
  }
  return netstack_l2addrstr(*type, RTA_PAYLOAD(rta), RTA_DATA(rta));
}

// Returns the MTU as reported by netlink, or 0 if none was reported.
static inline uint32_t
netstack_iface_mtu(const struct netstack_iface* ni){
  const struct rtattr* rta = netstack_iface_attr(ni, IFLA_MTU);
  uint32_t ret;
  return netstack_rtattrcpy_exact(rta, &ret, sizeof(ret)) ? ret : 0;
}

// Returns the link type (as opposed to the device type, as returned by
// netstack_iface_type
static inline int
netstack_iface_link(const struct netstack_iface* ni){
  const struct rtattr* rta = netstack_iface_attr(ni, IFLA_LINK);
  int ret;
  return netstack_rtattrcpy_exact(rta, &ret, sizeof(ret)) ? ret : 0;
}

// Returns the queuing discipline, or NULL if none was reported. The return is
// heap-allocated, and must be free()d by the caller.
char* netstack_iface_qdisc(const struct netstack_iface* ni);

// Returns interface stats if they were reported, filling in the stats object
// and returning 0. Returns -1 if there were no stats.
static inline bool
netstack_iface_stats(const struct netstack_iface* ni, struct rtnl_link_stats* stats){
  const struct rtattr* rta = netstack_iface_attr(ni, IFLA_STATS);
  return netstack_rtattrcpy_exact(rta, stats, sizeof(*stats));
}

// information about hardware queues. a value of -1 indicates that the driver
// does not provide information about the relevant field. different queues
// are typically mapped to different interrupts. these interrupts can then be
// distributed across cores to achieve parallelism in irq handling.
typedef struct netstack_iface_qcounts {
  // cards with multiple receive queues typically support simple rules to
  // distribute flows among the queues (to avoid reordering within a flow).
  int rx;
  int tx;
  int combined;
  int xdp;
} netstack_iface_qcounts;

void netstack_iface_queuecounts(const struct netstack_iface* ni,
                                struct netstack_iface_qcounts* nqc);

// Functions for inspecting netstack_neighs
const struct rtattr* netstack_neigh_attr(const struct netstack_neigh* nn, int attridx);
unsigned netstack_neigh_family(const struct netstack_neigh* nn); // always AF_UNSPEC
int netstack_neigh_index(const struct netstack_neigh* nn);
// A bitmask of NUD_{INCOMPLETE, REACHABLE, STALE, DELAY, PROBE, FAILED,
//                   NOARP, PERMANENT}
unsigned netstack_neigh_state(const struct netstack_neigh* nn);
unsigned netstack_neigh_flags(const struct netstack_neigh* nn);
unsigned netstack_neigh_type(const struct netstack_neigh* nn);

// Is this confirmed as reachable?
static inline bool
netstack_neigh_reachable(const struct netstack_neigh* nn){
  return netstack_neigh_state(nn) & NUD_REACHABLE;
}

// Is this entry stale?
static inline bool
netstack_neigh_stale(const struct netstack_neigh* nn){
  return netstack_neigh_state(nn) & NUD_STALE;
}

// Is this entry waiting for a timer?
static inline bool
netstack_neigh_delay(const struct netstack_neigh* nn){
  return netstack_neigh_state(nn) & NUD_DELAY;
}

// Is the entry being reprobed?
static inline bool
netstack_neigh_probe(const struct netstack_neigh* nn){
  return netstack_neigh_state(nn) & NUD_PROBE;
}

// Is this an invalidated cache entry?
static inline bool
netstack_neigh_failed(const struct netstack_neigh* nn){
  return netstack_neigh_state(nn) & NUD_FAILED;
}

// Does this device operate without a destination host cache?
static inline bool
netstack_neigh_noarp(const struct netstack_neigh* nn){
  return netstack_neigh_state(nn) & NUD_NOARP;
}

// Is this a permanent (admin-configured) neighbor cache entry?
static inline bool
netstack_neigh_permanent(const struct netstack_neigh* nn){
  return netstack_neigh_state(nn) & NUD_PERMANENT;
}

static inline bool
netstack_neigh_proxyarp(const struct netstack_neigh* nn){
  return netstack_neigh_flags(nn) & NTF_PROXY;
}

static inline bool
netstack_neigh_ipv6router(const struct netstack_neigh* nn){
  return netstack_neigh_flags(nn) & NTF_ROUTER;
}

static inline char*
netstack_l3addrstr(int fam, const void* addr, char* str, size_t slen){
  if(!inet_ntop(fam, addr, str, slen)){
    return NULL;
  }
  return str;
}

static inline int
netstack_rtattr_l3addr(int fam, const struct rtattr* rta,
                       void* addr, size_t* alen){
  if(fam == AF_INET){
    if(*alen < 4){
      return -1;
    }
    *alen = 4;
  }else if(fam == AF_INET6){
    if(*alen < 16){
      return -1;
    }
    *alen = 16;
  }else{
    return -1;
  }
  if(RTA_PAYLOAD(rta) != *alen){
    return -1;
  }
  memcpy(addr, RTA_DATA(rta), *alen);
  return 0;
}

static inline char*
netstack_rtattr_l3addrstr(int fam, const struct rtattr* rta,
                          char* str, size_t slen){
  size_t alen; // 4 for IPv4, 16 for IPv6
  if(fam == AF_INET){
    alen = 4;
  }else if(fam == AF_INET6){
    alen = 16;
  }else{
    return NULL;
  }
  if(RTA_PAYLOAD(rta) != alen){
    return NULL;
  }
  return netstack_l3addrstr(fam, RTA_DATA(rta), str, slen);
}

// Returns true iff there is an NDA_DST layer 3 address associated with this
// entry, *and* it can be transformed into a presentable string, *and* buf is
// large enough to hold the result. buflen ought be at least INET6_ADDRSTRLEN.
// family will hold the result of netstack_neigh_family() (assuming that an
// NDA_DST rtattr was indeed present).
static inline bool netstack_neigh_l3addrstr(const struct netstack_neigh* nn,
                                            char* buf, size_t buflen,
                                            unsigned* family){
  const struct rtattr* nnrta = netstack_neigh_attr(nn, NDA_DST);
  if(nnrta == NULL){
    return false;
  }
  *family = netstack_neigh_family(nn);
  if(!netstack_rtattr_l3addrstr(*family, nnrta, buf, buflen)){
    return false;
  }
  return true;
}

// Returns true iff there is an NDA_LLADDR layer 2 address associated with this
// entry, *and* buf is large enough to hold it. buflen ought generally be at
// least ETH_ALEN bytes.
static inline bool netstack_neigh_l2addr(const struct netstack_neigh* nn,
                                         void* buf, size_t buflen){
  const struct rtattr* l2rta = netstack_neigh_attr(nn, NDA_LLADDR);
  if(l2rta == NULL){
    return false;
  }
  if(buflen < RTA_PAYLOAD(l2rta)){
    return false;
  }
  memcpy(buf, RTA_DATA(l2rta), RTA_PAYLOAD(l2rta));
  return true;
}

// Returns non-NULL iff there is an NDA_LLADDR layer 2 address associated with
// this entry, *and* it can be transformed into a presentable string, *and*
// memory is successfully allocated to hold the result. The result must in that
// case be free()d by the caller.
static inline char* netstack_neigh_l2addrstr(const struct netstack_neigh* nn){
  const struct rtattr* l2rta = netstack_neigh_attr(nn, NDA_LLADDR);
  if(l2rta == NULL){
    return NULL;
  }
  char* llstr = netstack_l2addrstr(netstack_neigh_type(nn),
                                   RTA_PAYLOAD(l2rta), RTA_DATA(l2rta));
  return llstr;
}

// Returns true if an NDA_CACHEINFO rtattr is present, in which case cinfo will
// be filled in with the cache statistics for this entry.
static inline bool netstack_neigh_cachestats(const struct netstack_neigh* nn,
                                             struct nda_cacheinfo* cinfo){
  const struct rtattr* rta = netstack_neigh_attr(nn, NDA_CACHEINFO);
  if(rta == NULL){
    return false;
  }
  memcpy(cinfo, RTA_DATA(rta), RTA_PAYLOAD(rta));
  return true;
}

// Functions for inspecting netstack_addrs
const struct rtattr* netstack_addr_attr(const struct netstack_addr* na, int attridx);
unsigned netstack_addr_family(const struct netstack_addr* na);
unsigned netstack_addr_prefixlen(const struct netstack_addr* na);
unsigned netstack_addr_flags(const struct netstack_addr* na);
unsigned netstack_addr_scope(const struct netstack_addr* na);
int netstack_addr_index(const struct netstack_addr* na);

// Returns 0 iff there is an IFA_ADDRESS layer 3 address associated with this
// entry, *and* addr is large enough to hold the result. addr's size is
// passed in *alen, and the resulting size is returned there. alen ought be
// at least 16, to handle 128-bit IPv6 addresses. family will hold the result
// of netstack_addr_family() (assuming that an IFA_ADDRESS rtattr was indeed
// present). IFA_ADDRESS is the same as IFA_LOCAL on a broadcast interface; on
// point-to-point, it is the opposite end. IPv6 doesn't use IFA_LOCAL.
static inline int
netstack_addr_address(const struct netstack_addr* na, void* addr,
                      size_t* alen, unsigned* family){
  const struct rtattr* narta = netstack_addr_attr(na, IFA_ADDRESS);
  if(narta == NULL){
    return -1;
  }
  *family = netstack_addr_family(na);
  if(netstack_rtattr_l3addr(*family, narta, addr, alen)){
    return -1;
  }
  return 0;
}

// Returns true iff there is an IFA_ADDRESS layer 3 address associated with this
// entry, *and* it can be transformed into a presentable string, *and* buf is
// large enough to hold the result. buflen ought be at least INET6_ADDRSTRLEN.
// family will hold the result of netstack_addr_family() (assuming that an
// IFA_ADDRESS rtattr was indeed present). IFA_ADDRESS is the same as IFA_LOCAL
// on a broadcast interface; on point-to-point, it is the opposite end.
// IPv6 doesn't use IFA_LOCAL.
static inline char*
netstack_addr_addressstr(const struct netstack_addr* na, char* buf,
                         size_t buflen, unsigned* family){
  const struct rtattr* narta = netstack_addr_attr(na, IFA_ADDRESS);
  if(narta == NULL){
    return NULL;
  }
  *family = netstack_addr_family(na);
  if(!netstack_rtattr_l3addrstr(*family, narta, buf, buflen)){
    return NULL;
  }
  return buf;
}

// Returns 0 iff there is an IFA_LOCAL layer 3 address associated with this
// entry, *and* addr is large enough to hold the result. addr's size is
// passed in *alen, and the resulting size is returned there. alen ought be
// at least 16, to handle 128-bit IPv6 addresses. family will hold the result
// of netstack_addr_family() (assuming that an IFA_LOCAL rtattr was indeed
// present).
static inline int
netstack_addr_local(const struct netstack_addr* na, void* addr,
                    size_t* alen, unsigned* family){
  const struct rtattr* narta = netstack_addr_attr(na, IFA_LOCAL);
  if(narta == NULL){
    return -1;
  }
  *family = netstack_addr_family(na);
  if(netstack_rtattr_l3addr(*family, narta, addr, alen)){
    return -1;
  }
  return 0;
}

// Returns true iff there is an IFA_LOCAL layer 3 address associated with this
// entry, *and* it can be transformed into a presentable string, *and* buf is
// large enough to hold the result. buflen ought be at least INET6_ADDRSTRLEN.
// family will hold the result of netstack_addr_family() (assuming that an
// IFA_LOCAL rtattr was indeed present).
static inline char*
netstack_addr_localstr(const struct netstack_addr* na, char* buf,
                       size_t buflen, unsigned* family){
  const struct rtattr* narta = netstack_addr_attr(na, IFA_LOCAL);
  if(narta == NULL){
    return NULL;
  }
  *family = netstack_addr_family(na);
  if(!netstack_rtattr_l3addrstr(*family, narta, buf, buflen)){
    return NULL;
  }
  return buf;
}

// Returns the address label, or NULL if none was reported. The return is
// heap-allocated, and must be free()d by the caller.
char* netstack_addr_label(const struct netstack_addr* na);

// Returns address cacheinfo if they were reported, filling in the cinfo object
// and returning 0. Returns -1 if there was no such info.
static inline bool
netstack_addr_cacheinfo(const struct netstack_addr* na, struct ifa_cacheinfo* cinfo){
  const struct rtattr* rta = netstack_addr_attr(na, IFA_CACHEINFO);
  return netstack_rtattrcpy_exact(rta, cinfo, sizeof(*cinfo));
}

// Functions for inspecting netstack_routes. There is no netstack_route_index()
// because routes can have both incoming (RTA_IIF) and outgoing (RTA_OIF) ifaces.
const struct rtattr* netstack_route_attr(const struct netstack_route* nr, int attridx);
unsigned netstack_route_family(const struct netstack_route* nr);
unsigned netstack_route_dst_len(const struct netstack_route* nr);
unsigned netstack_route_src_len(const struct netstack_route* nr);
unsigned netstack_route_tos(const struct netstack_route* nr);
// Routing tables are indexed 0-255
unsigned netstack_route_table(const struct netstack_route* nr);
unsigned netstack_route_protocol(const struct netstack_route* nr);
unsigned netstack_route_scope(const struct netstack_route* nr);
unsigned netstack_route_type(const struct netstack_route* nr);
unsigned netstack_route_flags(const struct netstack_route* nr);

// default routes are those with 0-length destinations
static inline bool netstack_route_default(const struct netstack_route* nr){
  return !netstack_route_dst_len(nr);
}

static inline bool netstack_route_notify(const struct netstack_route* nr){
  return netstack_route_flags(nr) & RTM_F_NOTIFY;
}

// Was the route cloned from another route?
static inline bool netstack_route_cloned(const struct netstack_route* nr){
  return netstack_route_flags(nr) & RTM_F_CLONED;
}

static inline bool netstack_route_equalize(const struct netstack_route* nr){
  return netstack_route_flags(nr) & RTM_F_EQUALIZE;
}

static inline bool
netstack_route_str(const struct netstack_route* nr, int attr, char* buf,
                   size_t buflen, unsigned* family){
  const struct rtattr* nrrta = netstack_route_attr(nr, attr);
  if(nrrta == NULL){
    return false;
  }
  *family = netstack_route_family(nr);
  if(!netstack_rtattr_l3addrstr(*family, nrrta, buf, buflen)){
    return false;
  }
  return true;
}

// Returns 0 iff there is an RTA_DST layer 3 address associated with this
// entry, *and* addr is large enough to hold the result. addr's size is
// passed in *alen, and the resulting size is returned there. alen ought be
// at least 16, to handle 128-bit IPv6 addresses. family will hold the result
// of netstack_route_family() (assuming that an RTA_DST rtattr was indeed
// present).
static inline int
netstack_route_dst(const struct netstack_route* nr, void* addr,
                   size_t* alen, unsigned* family){
  const struct rtattr* nrrta = netstack_route_attr(nr, RTA_DST);
  if(nrrta == NULL){
    return -1;
  }
  *family = netstack_route_family(nr);
  if(netstack_rtattr_l3addr(*family, nrrta, addr, alen)){
    return -1;
  }
  return 0;
}

static inline bool netstack_route_dststr(const struct netstack_route* nr,
                                         char* buf, size_t buflen,
                                         unsigned* family){
  return netstack_route_str(nr, RTA_DST, buf, buflen, family);
}

// Returns 0 iff there is an RTA_SRC layer 3 address associated with this
// entry, *and* addr is large enough to hold the result. addr's size is
// passed in *alen, and the resulting size is returned there. alen ought be
// at least 16, to handle 128-bit IPv6 addresses. family will hold the result
// of netstack_route_family() (assuming that an RTA_SRC rtattr was indeed
// present).
static inline int
netstack_route_src(const struct netstack_route* nr, void* addr,
                   size_t* alen, unsigned* family){
  const struct rtattr* nrrta = netstack_route_attr(nr, RTA_SRC);
  if(nrrta == NULL){
    return -1;
  }
  *family = netstack_route_family(nr);
  if(netstack_rtattr_l3addr(*family, nrrta, addr, alen)){
    return -1;
  }
  return 0;
}

static inline bool netstack_route_srcstr(const struct netstack_route* nr,
                                         char* buf, size_t buflen,
                                         unsigned* family){
  return netstack_route_str(nr, RTA_SRC, buf, buflen, family);
}

// Returns 0 iff there is an RTA_GATEWAY layer 3 address associated with this
// entry, *and* addr is large enough to hold the result. addr's size is
// passed in *alen, and the resulting size is returned there. alen ought be
// at least 16, to handle 128-bit IPv6 addresses. family will hold the result
// of netstack_route_family() (assuming that an RTA_GATEWAY rtattr was indeed
// present).
static inline int
netstack_route_gateway(const struct netstack_route* nr, void* addr,
                       size_t* alen, unsigned* family){
  const struct rtattr* nrrta = netstack_route_attr(nr, RTA_GATEWAY);
  if(nrrta == NULL){
    return -1;
  }
  *family = netstack_route_family(nr);
  if(netstack_rtattr_l3addr(*family, nrrta, addr, alen)){
    return -1;
  }
  return 0;
}

static inline bool netstack_route_gatewaystr(const struct netstack_route* nr,
                                             char* buf, size_t buflen,
                                             unsigned* family){
  return netstack_route_str(nr, RTA_GATEWAY, buf, buflen, family);
}

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

static inline bool
netstack_route_cacheinfo(const struct netstack_route* nr,
                         struct rta_cacheinfo* cinfo){
  const struct rtattr* rta = netstack_route_attr(nr, RTA_CACHEINFO);
  return netstack_rtattrcpy_exact(rta, cinfo, sizeof(*cinfo));
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
    default: return "?";
  }
}

static inline const char*
netstack_route_scopestr(unsigned scope){
  switch(scope){
    case RT_SCOPE_UNIVERSE: return "global"; // global route
    case RT_SCOPE_SITE: return "site"; // interior route in the local AS
    case RT_SCOPE_LINK: return "link"; // route on this link
    case RT_SCOPE_HOST: return "host"; // route on the local host
    case RT_SCOPE_NOWHERE: return "nowhere"; // destination doesn't exist
    default: return "?";
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
  // If set, do not cache the corresponding type of object.
  bool iface_notrack, addr_notrack, route_notrack, neigh_notrack;
  // Policy for initial object dump. _ASYNC will cause events for existing
  // objects, but netstack_create() may return before they've been received.
  // _BLOCK blocks netstack_create() from returning until all initial
  // enumeration events have been received. _NONE inhibits initial enumeration.
  enum {
    NETSTACK_INITIAL_EVENTS_ASYNC,
    NETSTACK_INITIAL_EVENTS_BLOCK,
    NETSTACK_INITIAL_EVENTS_NONE,
  } initial_events;
  // logging callback. if NULL, the library will not log. netstack_stderr_diag
  // can be provided to dump to stderr, or provide your own function.
  void (*diagfxn)(const char* fmt, ...);
} netstack_opts;

// provide this as netstack_opts->diagfxn to get freeform diagnostics dumped
// to stderr.
void netstack_stderr_diag(const char* fmt, ...);

// Opts may be NULL, in which case the defaults will be used.
struct netstack* netstack_create(const netstack_opts* opts);
int netstack_destroy(struct netstack* ns);

// Count of interfaces in the active store, and bytes used to represent them in
// total. If iface_notrack is set, these will always return 0.
unsigned netstack_iface_count(const struct netstack* ns);
uint64_t netstack_iface_bytes(const struct netstack* ns);

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
// This format is subject to arbitrary change.
int netstack_print_iface(const struct netstack_iface* ni, FILE* out);
int netstack_print_addr(const struct netstack_addr* na, FILE* out);
int netstack_print_route(const struct netstack_route* nr, FILE* out);
int netstack_print_neigh(const struct netstack_neigh* nn, FILE* out);
int netstack_print_stats(const netstack_stats* stats, FILE* out);

// State for streaming enumerations (enumerations taking place over several
// calls). It's exposed in this header so that callers can easily define one on
// their stacks. Don't mess with it. Zero it out to start a new enumeration.
typedef struct netstack_enumerator {
  uint32_t nonce;
  uint32_t slot;
  uintptr_t hnext;
} netstack_enumerator;

// Enumerate up to n netstack_ifaces via copy. offsets must have space for at
// least n elements, which will serve as offsets into objs. objs is a flat
// array of size obytes. streamer ought point to a zero-initialized
// netstack_enumerator to begin an enumeration operation. If
// netstack_iface_enumerate() is called again using this same streamer, the
// enumeration picks up where it left off. A NULL streamer is interpreted as a
// request for atomic enumeration; if there is not sufficient space to copy all
// objects, it is an error, and the copying will be aborted as soon as
// possible. Unlike other errors, n and obytes will be updated in this case to
// reflect the current necessary values.
//
// Returns -1 on error, due to invalid parameters, insufficient space for an
// atomic enumeraion, or failure to resume an enumeration (this can happen if
// too much has changed since the previous call--enumerations aren't really
// suitable for highly dynamic environments). No parameters are modified in
// this case (save the atomic case, as noted above). Otherwise, the number of objects
// copied r is returned, r <= the original *n. n is set to the number of
// objects remaining. obytes is set to the bytes required to copy the remaning
// objects. streamer is updated, if appropriate. The first r values of offsets
// give valid byte offsets into objs, and a (suitably-aligned) network_iface is
// present at each such offset. Their associated buffers are also present in
// objs. The pointers and bookkeeping within the netstack_ifaces have been
// updated so that the resulting objects can be used with the standard
// netstack_iface API. There is no need to call netstack_iface_abandon() on
// these objects.
//
// An enumeration operation is thus successfully terminated iff a non-negative
// number is returned, and *n and *obytes have both been set to 0. Note that
// a 0 could be returned without completion if objs is too small to copy the
// next object; in this case, neither *n nor *obytes would be 0.
int netstack_iface_enumerate(const struct netstack* ns,
                             uint32_t* offsets, int* n,
                             void* objs, size_t* obytes,
                             netstack_enumerator* streamer);

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
  fputc('A', vf);
  fputc(etype == NETSTACK_DEL ? '*' : ' ', vf);
  netstack_print_addr(na, vf);
}

static inline void
vnetstack_print_route(const struct netstack_route* nr, netstack_event_e etype, void* vf){
  fputc('R', vf);
  fputc(etype == NETSTACK_DEL ? '*' : ' ', vf);
  netstack_print_route(nr, vf);
}

static inline void
vnetstack_print_neigh(const struct netstack_neigh* nn, netstack_event_e etype, void* vf){
  fputc('N', vf);
  fputc(etype == NETSTACK_DEL ? '*' : ' ', vf);
  netstack_print_neigh(nn, vf);
}
#endif

#endif
