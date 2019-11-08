#include <mutex>
#include "main.h"

// Unit tests for the notrack functionality

// There must be at least some callback or some tracking. Verify that we can
// initialize with all tracking off save each one, then verify that we cannot
// initialize with all tracking and callbacks disabled.
TEST(NoTrack, NoWorkIsInvalid) {
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.iface_notrack = true;
  nopts.addr_notrack = true;
  nopts.route_notrack = true;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  EXPECT_EQ(0, netstack_destroy(ns));
  nopts.neigh_notrack = true;
  nopts.route_notrack = false;
  ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  EXPECT_EQ(0, netstack_destroy(ns));
  nopts.route_notrack = true;
  nopts.addr_notrack = false;
  ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  EXPECT_EQ(0, netstack_destroy(ns));
  nopts.addr_notrack = true;
  nopts.iface_notrack = false;
  ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  EXPECT_EQ(0, netstack_destroy(ns));
  nopts.iface_notrack = true;
  ns = netstack_create(&nopts);
  ASSERT_EQ(nullptr, ns);
}

// stashes the index of the callback subject interface
struct copycurry {
  std::mutex mlock;
  std::string name;
  int idx;
};

// Sets up the callback subject interface to be copied by the callback itself,
// then handed to an external thread.
static void
IntCopyCB(const netstack_iface* ni, netstack_event_e etype, void* curry) {
  if(etype != NETSTACK_MOD){
    return;
  }
  struct copycurry* cc = static_cast<struct copycurry*>(curry);
  std::lock_guard<std::mutex> guard(cc->mlock);
  if(cc->name == ""){ // only stash once
    char name[IFNAMSIZ];
    ASSERT_EQ(name, cc->name = netstack_iface_name(ni, name));
    cc->idx = netstack_iface_index(ni);
  }
}

// Verify that we can't look up an interface for which we got a callback
TEST(NoTrack, IfaceFailsLookup) {
  struct copycurry cc = {};
  cc.name = "";
  cc.idx = -1;
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = netstack_opts::NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.iface_notrack = true;
  nopts.iface_cb = IntCopyCB;
  nopts.iface_curry = &cc;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_NE("", cc.name);
  ASSERT_LT(-1, cc.idx);
  auto ni = netstack_iface_share_byname(ns, cc.name.c_str());
  ASSERT_EQ(nullptr, ni);
  ni = netstack_iface_share_byidx(ns, cc.idx);
  ASSERT_EQ(nullptr, ni);
  ASSERT_EQ(0, netstack_destroy(ns));
}
