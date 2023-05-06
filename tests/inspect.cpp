#include <mutex>
#include "main.h"

// Unit tests for running iface inspection from within callbacks

static void
IfaceCB(const netstack_iface* ni, netstack_event_e etype,
        void* curry __attribute__ ((unused))) {
  if(etype != NETSTACK_MOD){
    return;
  }
  struct rtnl_link_stats64 stats;
  ASSERT_TRUE(netstack_iface_stats(ni, &stats));
}

TEST(Inspect, IfaceProperties) {
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = netstack_opts::NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.iface_cb = IfaceCB;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_EQ(0, netstack_destroy(ns));
}

static void
AddrCB(const netstack_addr* na, netstack_event_e etype,
       void* curry __attribute__ ((unused))) {
  if(etype != NETSTACK_MOD){
    return;
  }
  struct ifa_cacheinfo stats;
  ASSERT_TRUE(netstack_addr_cacheinfo(na, &stats));
}

TEST(Inspect, AddressProperties) {
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = netstack_opts::NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.addr_cb = AddrCB;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_EQ(0, netstack_destroy(ns));
}

static void
RouteCB(const netstack_route* nr, netstack_event_e etype,
        void* curry __attribute__ ((unused))) {
  if(etype != NETSTACK_MOD){
    return;
  }
  EXPECT_GT(256, netstack_route_metric(nr));
}

TEST(Inspect, RouteProperties) {
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = netstack_opts::NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.route_cb = RouteCB;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_EQ(0, netstack_destroy(ns));
}
