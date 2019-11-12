#include <atomic>
#include "main.h"

TEST(Netstack, CreatenullptrOpts) {
  struct netstack* ns = netstack_create(nullptr);
  ASSERT_NE(nullptr, ns);
  ASSERT_EQ(0, netstack_destroy(ns));
}

TEST(Netstack, CreateDefaultOpts) {
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_EQ(0, netstack_destroy(ns));
}

// providing a curry without a corresponding callback ought result in failure
TEST(Netstack, RefuseCurryWithoutCB) {
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.iface_curry = &nopts;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_EQ(nullptr, ns);
  nopts.iface_curry = nullptr;
  nopts.addr_curry = &nopts;
  ns = netstack_create(&nopts);
  ASSERT_EQ(nullptr, ns);
  nopts.addr_curry = nullptr;
  nopts.route_curry = &nopts;
  ns = netstack_create(&nopts);
  ASSERT_EQ(nullptr, ns);
  nopts.route_curry = nullptr;
  nopts.neigh_curry = &nopts;
  ns = netstack_create(&nopts);
  ASSERT_EQ(nullptr, ns);
  nopts.neigh_curry = nullptr;
  // we ought now be able to create successfully
  ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_EQ(0, netstack_destroy(ns));
}

void ifacecb(const netstack_iface* ni __attribute__ ((unused)),
             netstack_event_e etype __attribute__ ((unused)), void* curry) {
  std::atomic<int>* post = static_cast<std::atomic<int>*>(curry);
  ++*post;
}

TEST(Netstack, CreateInitialEventsNone) {
  std::atomic<int> shouldnt_post{0};
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = netstack_opts::NETSTACK_INITIAL_EVENTS_NONE;
  nopts.iface_cb = ifacecb;
  nopts.iface_curry = &shouldnt_post;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  netstack_stats stats;
  // races against a possible event, flaky :/ FIXME
  ASSERT_NE(nullptr, netstack_sample_stats(ns, &stats));
  EXPECT_EQ(0, stats.ifaces);
  EXPECT_EQ(0, stats.iface_events);
  ASSERT_EQ(0, netstack_destroy(ns));
  ASSERT_EQ(0, shouldnt_post);
}

TEST(Netstack, CreateInitialEventsBlock) {
  std::atomic<int> post{0};
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = netstack_opts::NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.iface_cb = ifacecb;
  nopts.iface_curry = &post;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  int postcopy = post;
  ASSERT_NE(0, postcopy);
  netstack_stats stats;
  ASSERT_NE(nullptr, netstack_sample_stats(ns, &stats));
  EXPECT_LT(0, stats.ifaces);
  EXPECT_LT(0, stats.iface_events);
  ASSERT_EQ(0, netstack_destroy(ns));
  ASSERT_EQ(postcopy, post);
}

// verify that iface_count and iface_bytes are non-zero (assumes a device FIXME)
TEST(Netstack, IfaceCacheStats) {
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = netstack_opts::NETSTACK_INITIAL_EVENTS_BLOCK;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  unsigned count = netstack_iface_count(ns);
  ASSERT_NE(0, count);
  ASSERT_LT(count, netstack_iface_bytes(ns));
  netstack_stats stats;
  ASSERT_NE(nullptr, netstack_sample_stats(ns, &stats));
  EXPECT_LT(0, stats.ifaces);
  EXPECT_LT(0, stats.iface_events);
  ASSERT_EQ(0, netstack_destroy(ns));
}

// If iface_notrack is set, we ought see 0 ifaces in the cache
TEST(Netstack, IfaceCountNoCache) {
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = netstack_opts::NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.iface_notrack = true;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_EQ(0, netstack_iface_count(ns));
  netstack_stats stats;
  ASSERT_NE(nullptr, netstack_sample_stats(ns, &stats));
  EXPECT_EQ(0, stats.ifaces);
  EXPECT_EQ(0, stats.iface_events);
  ASSERT_EQ(0, netstack_destroy(ns));
}
