#include <mutex>
#include "main.h"

// Unit tests for copying/sharing objects previously registered via callback,
// from outside of the callback context.

// stashes the index of the callback subject interface
struct copycurry {
  std::mutex mlock;
  int idx;
};

// Sets up the callback subject interface to be copied or shared by a thread
// external to the libnetstack event engine.
static void
ExternalCB(const netstack_iface* ni, netstack_event_e etype, void* curry) {
  if(etype != NETSTACK_MOD){
    return;
  }
  struct copycurry* cc = static_cast<struct copycurry*>(curry);
  cc->mlock.lock();
  cc->idx = netstack_iface_index(ni);
  cc->mlock.unlock();
}

// Test deep copying of an interface from outside of its callback context
TEST(IdxLookup, IfaceDeepCopy) {
  struct copycurry cc = {};
  cc.idx = -1;
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = netstack_opts::NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.iface_cb = ExternalCB;
  nopts.iface_curry = &cc;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  netstack_iface* ni = netstack_iface_copy_byidx(ns, cc.idx);
  ASSERT_NE(nullptr, ni);
  char name[IFNAMSIZ];
  ASSERT_EQ(name, netstack_iface_name(ni, name));
  netstack_iface* ni2 = netstack_iface_copy_byidx(ns, cc.idx);
  ASSERT_NE(nullptr, ni2);
  char name2[IFNAMSIZ];
  ASSERT_EQ(name2, netstack_iface_name(ni2, name2));
  ASSERT_NE(ni, ni2);
  ASSERT_STREQ(name, name2);
  netstack_iface_abandon(ni2);
  cc.mlock.unlock();
  ASSERT_EQ(0, netstack_destroy(ns));
  netstack_iface_abandon(ni); // we should still be able to use it
}

// Test sharing of an interface from outside of its callback context
TEST(IdxLookup, IfaceShare) {
  struct copycurry cc = {};
  cc.idx = -1;
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = netstack_opts::NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.iface_cb = ExternalCB;
  nopts.iface_curry = &cc;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  const netstack_iface* ni = netstack_iface_share_byidx(ns, cc.idx);
  ASSERT_NE(nullptr, ni);
  char name[IFNAMSIZ];
  ASSERT_EQ(name, netstack_iface_name(ni, name));
  const netstack_iface* ni2 = netstack_iface_share_byidx(ns, cc.idx);
  ASSERT_NE(nullptr, ni2);
  char name2[IFNAMSIZ];
  ASSERT_EQ(name2, netstack_iface_name(ni2, name2));
  ASSERT_EQ(ni, ni2);
  ASSERT_STREQ(name, name2);
  netstack_iface_abandon(ni2);
  cc.mlock.unlock();
  ASSERT_EQ(0, netstack_destroy(ns));
  netstack_iface_abandon(ni); // we should still be able to use it
}
