# libnetstack
Small C library for networking stack query / events

by Nick Black <dankamongmen@gmail.com>

[![Build Status](https://drone.dsscaw.com:4443/api/badges/dankamongmen/libnetstack/status.svg)](https://drone.dsscaw.com:4443/dankamongmen/libnetstack)

libnetstack allows netstack objects to be created, queried, and destroyed. When
created, a netstack discovers all networking elements in its network namespace
(interfaces, routes, addresses, neighbors, etc.), and registers for netlink
messages announcing any changes. These changes can be passed back to the user
via callbacks. It furthermore keeps a cache of these elements up-to-date based
on netlink, and the user can query this cache at any time. Design goals
included minimal footprints for both memory and compute, while supporting fast
lookups in the presence of millions of routes.

## Why not just use [libnl-route](https://www.infradead.org/~tgr/libnl/doc/api/group__rtnl.html)?

Feel free to use libnl-route. I wasn't enamored of some of its API decisions.
Both libraries are solving the same general problem, and both use
[libnl](https://www.infradead.org/~tgr/libnl/doc/api/group__core.html).
Libnetstack is Apache-licensed.

## Requirements

* CMake and a C compiler
* libnl 3.4.0+

## Use

First, a `struct netstack` must be created using `netstack_create()`. It
accepts a `netstack_opts` structure for configuration. On failure, NULL is
returned. A program may have an many netstacks as it likes, though I can't see
much point in more than one in a process.

```
struct netstack;
struct netstack* netstack_create(const netstack_opts* opts);
int netstack_destroy(struct netstack* ns);
```

When done using the netstack, call `netstack_destroy()` to release its
resources and perform consistency checks. On failure, non-zero is returned, but
this can usually be ignored by the caller.

### Options

Usually, the caller will want to at least configure some callbacks using the
`netstack_opts` structure passed to `netstack_create()`. A callback and a curry
may be configured for each different kind of object. If the callback is NULL,
the curry must also be NULL. If the callback is NULL, the default callback of
printing the messages to stdout is used. To truly ignore an event class, write
a do-nothing function, and pass it via `netstack_opts`.

```
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
```
