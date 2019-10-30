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
