# [libnetstack](https://nick-black.com/dankwiki/index.php/Libnetstack)
A small C library for tracking and querying the local networking stack

by Nick Black <dankamongmen@gmail.com>

[![Build Status](https://drone.dsscaw.com:4443/api/badges/dankamongmen/libnetstack/status.svg)](https://drone.dsscaw.com:4443/dankamongmen/libnetstack)

<p align="center">
<img width="640" height="320" src="tools/libnetstack.jpg"/>
</p>

libnetstack allows `netstack` objects to be created, queried, and destroyed.
When created, a `netstack` discovers all networking elements in its network
namespace (interfaces, routes, addresses, neighbors, etc.—see `CLONE_NET`), and
registers for netlink messages announcing any changes. These changes can be
passed back to the user via callbacks. It furthermore keeps a cache of these
elements up-to-date based on netlink, and the user can query this cache at any
time. Design goals included minimal footprints for both memory and compute,
while supporting fast lookups in the presence of millions of routes.

* [Why not just use libnl-route?](#why-not-just-use-libnl-route)
  * [Why not just use ioctl()s, as Stevens taught us?](#why-not-just-use-ioctls-as-stevens-taught-us)
  * [Why not just use netlink(3) directly?](#why-not-just-use-netlink3-directly)
* [Getting libnetstack](#getting-libnetstack)
  * [Packages](#packages)
  * [Requirements](#requirements)
  * [Building](#building)
* [Use](#use)
* [Initial enumeration events](#initial-enumeration-events)
* [Object types](#object-types)
* [Options](#options)
* [Accessing cached objects](#accessing-cached-objects)
* [Enumerating cached objects](#enumerating-cached-objects)
* [Querying objects](#querying-objects)
  * [Interfaces](#interfaces)
  * [Addresses](#addresses)
  * [Routes](#routes)
  * [Neighbors](#neighbors)
* [Examples](#examples)

## Why not just use [libnl-route](https://www.infradead.org/~tgr/libnl/doc/api/group__rtnl.html)?

Feel free to use libnl-route. I wasn't enamored of some of its API decisions.
Both libraries are solving the same general problem, both only support Linux,
and both use [libnl](https://www.infradead.org/~tgr/libnl/doc/api/group__core.html).
I intend to support FreeBSD in the future.

I believe libnetstack to be more performant on the very complex networking
stacks present in certain environments, and to better serve heavily parallel
access. The typical user is unlikely to see a meaningful performance
difference.

Libnetstack is Apache-licensed, whereas libnl-route is LGPL.

### Why not just use `ioctl()`s, as Stevens taught us?

[UNIX Network Programming's](http://www.unpbook.com/) third and most recent
edition was 2003. Much has happened since then. The various `ioctl()`
mechanisms require polling, and are incomplete compared to rtnetlink(7).

### Why not just use netlink(3) directly?

It's a tremendous pain in the ass, I assure you.

## Getting libnetstack

### Packages

libnetstack is present in the [AUR](https://aur.archlinux.org/packages/libnetstack/).

Debian Unstable packages are available from [DSSCAW](https://www.dsscaw.com/apt.html).

### Requirements

* Core library: CMake, a C11 compiler, and libnl 3.4.0+
* Tests: a C++14 compiler and GoogleTest 1.9.0+

### Building

`mkdir build && cd build && cmake .. && make && make test && sudo make install`

You know the drill.

## Use

A `struct netstack` must first be created using `netstack_create()`. This
accepts a `netstack_opts` structure for configuration, including specification
of callbacks. `NULL` is returned on failure. A program may have an many
netstacks as it likes, though I don't personally see much point in more than
one in a process. This does not require any special privileges.

```c
struct netstack* netstack_create(const netstack_opts* opts);
int netstack_destroy(struct netstack* ns);
```

Once a `netstack` is no longer needed, call `netstack_destroy()` to release its
resources and perform consistency checks. On failure, non-zero is returned, but
this can usually be ignored by the caller.

The caller now interacts with the library in two ways: its registered callbacks
will be invoked for each event processed, and it can at any time access the
libnetstack cache. Multiple threads might invoke callbacks at once (though this
does not happen in the current implementation, it might in the future).
Ordering between different objects is not necessarily preserved, but events
for the same object ("same" meaning "same lookup key", see below) are
serialized.

## Initial enumeration events

By default, upon creation of a `netstack` all objects will be enumerated,
resulting in a slew of events. This behavior can be changed with the
`initial_events` field in `network_opts`:

* `NETSTACK_INITIAL_EVENTS_ASYNC`: The default. Upon creation, objects will be
  enumerated, but `netstack_create()` will return after sending the necessary
  requests. Events might arrive before or after `netstack_create()` returns.
* `NETSTACK_INITIAL_EVENTS_BLOCK`: Don't return from `netstack_create()` until
  all objects have been enumerated. If used, the cache may be safely
  interrogated once `netstack_create()` returns. Otherwise, existing objects
  might not show up for a short time.
* `NETSTACK_INITIAL_EVENTS_NONE`: Don't perform the initial enumeration.

## Object types

Four object types are currently supported:

* _ifaces_, corresponding to network devices both physical and virtual. There
  is a one-to-one correspondence to elements in sysfs's `/class/net` node, and
  also to the outputs of `ip link list`.

The remaining objects are all associated with a single _iface_, but
multiple _ifaces_ might each lay claim to overlapping objects. For instance, it
is possible (though usually pathological) to have the same _address_ on two
different interfaces. This will result in two _address_ objects, each reachable
through a different _iface_.

* _addresses_, corresponding to local layer 3 addresses. An _address_ might
  have a corresponding _broadcast address_.
* _neighbors_, corresponding to l3 addresses thought to be reachable via direct
  transmission. A _neighbor_ might have a corresponding _link address_.
  In IPv4, these objects are largely a function of ARP. In IPv6, they primarily
  result from NDP.
* _routes_, corresponding to a destination l3 network. _routes_ specify an
  associated _source address_. This _source address_ will typically correspond
  to a known local _address_, but this cannot be assumed (to construct an
  example from the command line, add an IP to an interface, add a route
  specifying that source IP, and remove the address).

In general, objects correspond to `rtnetlink(7)` message type families.
Multicast support is planned.

## Options

Usually, the caller will want to at least configure some callbacks using the
`netstack_opts` structure passed to `netstack_create()`. A callback and a curry
may be configured for each different kind of object. If the callback is `NULL`,
the curry must also be `NULL`.

```c
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
```

## Accessing cached objects

Since events can arrive at any time, invalidating the object cache, it is
necessary that the caller either:

* increment a reference counter, yielding a pointer to an immutable object
   which must be referenced down,
* deep-copy objects out upon access, yielding a mutable object which must be
   destroyed, or
* extract any elements (via copy) without gaining access to the greater object.

All three mechanisms are supported. Each mechanism takes place while locking at
least part of the `netstack` internals, possibly blocking other threads
(including those of the `netstack` itself, potentially causing kernel events to
be dropped). The first and third are generally the most useful ways to operate.

When only a small amount of information is needed, the third method is
simplest and most effective. The call is provided a key (interface index or
name). While locked, the corresponding object is found, and the appropriate
data are copied out. The lock is released, and the copied data are returned.
Note that it is impossible using this method to get an atomic view of
multiple attributes, since the object might change (or be destroyed) between
calls. For the API, see "[Querying objects](#querying-objects)" below.

When the object will be needed for multiple operations, it's generally better
to use the reference-counter approach. Compared to the extraction method, this
allows atomic inspection of multiple attributes, and requires only one lookup
instead of N. While the object is held, it cannot be destroyed by the `netstack`,
but it might be replaced. It is thus possible for multiple objects in this
situation to share the same key, something that never happens in the real world
(or in the `netstack`'s cache). Failing to down the reference counter is
effectively a memory leak.

```c
// Take a reference on some netstack iface for read-only use in the client.
// There is no copy, but the object still needs to be freed by a call to
// netstack_iface_abandon().
const struct netstack_iface* netstack_iface_share_byname(struct netstack* ns, const char* name);
const struct netstack_iface* netstack_iface_share_byidx(struct netstack* ns, int idx);
```

The second mechanism, a deep copy, is only rarely useful. It leaves no residue
outside the caller, and is never shared when created. This could be important
for certain control flows and memory architectures.

```c
// Copy out a netstack iface for arbitrary use in the client. This is a
// heavyweight copy, and must be freed using netstack_iface_destroy(). You
// would usually be better served by netstack_iface_share_*().
struct netstack_iface* netstack_iface_copy_byname(struct netstack* ns, const char* name);
struct netstack_iface* netstack_iface_copy_byidx(struct netstack* ns, int idx);
```

Shares and copies can occur from within a callback. If you want to use the
object that was provided in the callback, this can be done without a lookup
or taking any additional locks:

```c
// Copy/share a netstack_iface to which we already have a handle, for
// instance directly from the callback context. This is faster than the
// alternatives, as it needn't perform a lookup.
const struct netstack_iface* netstack_iface_share(const struct netstack_iface* ni);
struct netstack_iface* netstack_iface_copy(const struct netstack_iface* ni);
```

Whether deep-copied or shared, the object can and should be abandoned via
`netstack_iface_abandon()`. This should be done even if the `netstack` is
destroyed, with the implication that both shared and copied `netstack_iface`s
remains valid after a call to `netstack_destroy()`.

```c
// Release a netstack_iface acquired from the netstack through either a copy or
// a share operation. Note that while the signature claims constness, ns will
// actually presumably be mutated (via alias). It is thus imperative that the
// passed object not be used again by the caller!
void netstack_iface_abandon(const struct netstack_iface* ni);
```

## Enumerating cached objects

It is possible to get all the cached objects of a type via enumeration. This
requires providing a (possibly large) buffer into which data will be copied.
If the buffer is not large enough to hold all the objects, another call can be
made to get the next batch (it is technically possible to enumerate the objects
one-by-one using this method), but this is not guaranteed to be an atomic view
of the object class.

The number of objects currently cached can be queried, though this is no
guarantee that the number won't have changed by the time a subsequent
enumeration is requested:

```c
// Count of interfaces in the active store, and bytes used to represent them in
// total. If iface_notrack is set, these will always return 0.
unsigned netstack_iface_count(const struct netstack* ns);
uint64_t netstack_iface_bytes(const netstack* ns);
```

Enumeration currently always takes the form of a copy, never a share (shared
enumerations will be added if a compelling reason for them is found). Two
buffers must be provided for an enumeration request of up to `N` objects:
* `offsets`, an array of `N` `uint32_t`s, and
* `objs`, a character buffer of some size (`obytes`).

No more than `N` objects will be enumerated. If `objs` becomes exhausted, or
`N` objects do not exist, fewer than `N` will be enumerated. The number of
objects enumerated is returned, or -1 on error.

```c
// State for streaming enumerations (enumerations taking place over several
// calls). It's exposed in this header so that callers can easily define one on
// their stacks. Don't mess with it. Zero it out to start a new enumeration.
typedef struct netstack_enumerator {
  uint32_t nonce;
  uint32_t slot;
  struct netstack_iface* hnext;
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
```

The `streamer` parameter is used to stream through the objects. It must be
zeroed out prior to the first call of an enumeration sequence, and should not
be modified by the caller. Repeating a call with a `streamer` that has
already completed is not an error (0 will be returned, and `n` and `obytes`
will both be set to 0). An enumeration returning an error should not be retried
with the same `streamer`.

For a positive return value _r_, the _r_ values returned in `offsets` index
into `objs`. Each one is a (suitably-aligned) `struct netstack_iface`. These
`netstack_iface`s do *not* need to be fed to `netstack_iface_abandon()`.

## Querying objects

### Interfaces

### Addresses

### Routes

### Neigbors

Neighbors are described by the opaque `netstack_neigh` object. Neighbors can be
configured by the administrator or proxy ARP servers, but more typically they
follow a natural periodic discovery state machine. Many link types do not have
a concept of neighbors.

```c
const struct rtattr* netstack_neigh_attr(const struct netstack_neigh* nn, int attridx);
int netstack_neigh_index(const struct netstack_neigh* nn);
int netstack_neigh_family(const struct netstack_neigh* nn); // always AF_UNSPEC
unsigned netstack_neigh_flags(const struct netstack_neigh* nn);

static inline bool
netstack_neigh_proxyarp(const struct netstack_neigh* nn){
  return netstack_neigh_flags(nn) & NTF_PROXY;
}

static inline bool
netstack_neigh_ipv6router(const struct netstack_neigh* nn){
  return netstack_neigh_flags(nn) & NTF_ROUTER;
}

unsigned netstack_neigh_type(const struct netstack_neigh* nn);
// A bitmask of NUD_{INCOMPLETE, REACHABLE, STALE, DELAY, PROBE, FAILED,
//                   NOARP, PERMANENT}
unsigned netstack_neigh_state(const struct netstack_neigh* nn);

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
  if(!l3addrstr(*family, nnrta, buf, buflen)){
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
```

## Examples


