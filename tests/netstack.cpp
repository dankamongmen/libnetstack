#include <atomic>
#include "main.h"

TEST(Netstack, CreateNULLOpts) {
  struct netstack* ns = netstack_create(NULL);
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
  nopts.iface_curry = NULL;
  nopts.addr_curry = &nopts;
  ns = netstack_create(&nopts);
  ASSERT_EQ(nullptr, ns);
  nopts.addr_curry = NULL;
  nopts.route_curry = &nopts;
  ns = netstack_create(&nopts);
  ASSERT_EQ(nullptr, ns);
  nopts.route_curry = NULL;
  nopts.neigh_curry = &nopts;
  ns = netstack_create(&nopts);
  ASSERT_EQ(nullptr, ns);
  nopts.neigh_curry = NULL;
  // we ought now be able to create successfully
  ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_EQ(0, netstack_destroy(ns));
}

void ifacecb(const netstack_iface* ni, netstack_event_e etype, void* curry) {
  std::atomic<int>* post = static_cast<std::atomic<int>*>(curry);
  ++*post;
}

TEST(Netstack, CreateInitialEventsNone) {
  std::atomic<int> shouldnt_post{0};
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = NETSTACK_INITIAL_EVENTS_NONE;
  nopts.iface_cb = ifacecb;
  nopts.iface_curry = &shouldnt_post;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  sleep(1);
  ASSERT_EQ(0, netstack_destroy(ns));
  ASSERT_EQ(0, shouldnt_post);
}

TEST(Netstack, CreateInitialEventsBlock) {
  std::atomic<int> post{0};
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.iface_cb = ifacecb;
  nopts.iface_curry = &post;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  int postcopy = post;
  ASSERT_NE(0, postcopy);
  sleep(1);
  ASSERT_EQ(0, netstack_destroy(ns));
  ASSERT_EQ(postcopy, post);
}
