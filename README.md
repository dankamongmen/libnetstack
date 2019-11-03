# libnetstack
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

### Why not just use `ioctl()`s as Stevens taught us?

[UNIX Network Programming's](http://www.unpbook.com/) third and most recent
edition was 2003. Much has happened since then. The various `ioctl()`
mechanisms require polling, and are incomplete compared to rtnetlink(7).

### Why not just use netlink(3) directly?

It's a tremendous pain in the ass.

## Requirements

* Core library: CMake, a C11 compiler, and libnl 3.4.0+
* Tests: a C++14 compiler and GoogleTest 1.9.0+

## Use

A `struct netstack` must first be created using `netstack_create()`. This
accepts a `netstack_opts` structure for configuration, including specification
of callbacks. On failure, `NULL` is returned. A program may have an many
netstacks as it likes, though I don't personally see much point in more than
one in a process. This does not require any special privileges.

```
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

### Initial enumeration events

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

### Object types

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

### Options

Usually, the caller will want to at least configure some callbacks using the
`netstack_opts` structure passed to `netstack_create()`. A callback and a curry
may be configured for each different kind of object. If the callback is `NULL`,
the curry must also be `NULL`.

```
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

### Accessing cached objects

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
be dropped). The first and third are the most generally useful ways to operate.

When only a small amount of information is needed, the third method is
simplest and most effective. The call is provided a key (interface index or
name). While locked, the corresponding object is found, and the appropriate
data are copied out. The lock is released, and the copied data are returned.
Note that it is impossible using this method to get an atomic view of
multiple attributes, since the object might change (or be destroyed) between
calls. Aside from when an attribute of variable length is returned, there is
nothing to free.

When the object will be needed for multiple operations, it's generally better
to use the reference-counter approach. Compared to the extraction method, this
allows atomic inspection of multiple attributes, and requires only one lookup
instead of N. While the object is held, it cannot be destroyed by the `netstack`,
but it might be replaced. It is thus possible for multiple objects in this
situation to share the same key, something that never happens in the real world
(or in the `netstack`'s cache). Failing to down the reference counter is
effectively a memory leak.

```
// Take a reference on some netstack iface for read-only use in the client.
// There is no copy, but the object still needs to be freed by a call to
// netstack_iface_abandon().
const struct netstack_iface* netstack_iface_share_byname(struct netstack* ns, const char* name);
const struct netstack_iface* netstack_iface_share_byidx(struct netstack* ns, int idx);
```

The second mechanism, a deep copy, is only rarely useful. It leaves no residue
outside the caller, and is never shared when created. This could be important
for certain control flows and memory architectures.

```
// Copy out a netstack iface for arbitrary use in the client. This is a
// heavyweight copy, and must be freed using netstack_iface_destroy(). You
// would usually be better served by netstack_iface_share_*().
struct netstack_iface* netstack_iface_copy_byname(struct netstack* ns, const char* name);
struct netstack_iface* netstack_iface_copy_byidx(struct netstack* ns, int idx);
```

Shares and copies can occur from within a callback. If you want to use the
object that was provided in the callback, this can be done without a lookup
or taking any additional locks:

```
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

```
// Release a netstack_iface acquired from the netstack through either a copy or
// a share operation. Note that while the signature claims constness, ns will
// actually presumably be mutated (via alias). It is thus imperative that the
// passed object not be used again by the caller!
void netstack_iface_abandon(const struct netstack_iface* ni);
```

### Working with objects
