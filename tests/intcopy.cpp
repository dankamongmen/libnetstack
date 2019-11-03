#include <mutex>
#include "main.h"

// Unit tests for copying/sharing the subjects of callbacks from within the
// callback context

// stashes the index of the callback subject interface
struct copycurry {
  std::mutex mlock;
  const struct netstack_iface* ni1; // abandoned prior to nestack_destroy
  const struct netstack_iface* ni2; // abandoned following netstack_destroy
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
  if(cc->ni1 == nullptr){ // only stash once
    cc->ni1 = netstack_iface_copy(ni);
    cc->ni2 = netstack_iface_copy(ni);
  }
}

// Test deep copying of an interface from within its callback context
TEST(CopyIface, CallbackDeepCopy) {
  struct copycurry cc = {};
  cc.ni1 = nullptr;
  cc.ni2 = nullptr;
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.iface_cb = IntCopyCB;
  nopts.iface_curry = &cc;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_NE(nullptr, cc.ni1);
  ASSERT_NE(nullptr, cc.ni2);
  netstack_iface_abandon(cc.ni1);
  ASSERT_EQ(0, netstack_destroy(ns));
  netstack_iface_abandon(cc.ni2); // we should still be able to use it
}

static void
IntShareCB(const netstack_iface* ni, netstack_event_e etype, void* curry) {
  if(etype != NETSTACK_MOD){
    return;
  }
  struct copycurry* cc = static_cast<struct copycurry*>(curry);
  std::lock_guard<std::mutex> guard(cc->mlock);
  if(cc->ni1 == nullptr){ // only stash once
    cc->ni1 = netstack_iface_share(ni);
    cc->ni2 = netstack_iface_share(ni);
  }
}

// Test sharing of an interface from within its callback context
TEST(CopyIface, CallbackShare) {
  struct copycurry cc = {};
  cc.ni1 = nullptr;
  cc.ni2 = nullptr;
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.iface_cb = IntShareCB;
  nopts.iface_curry = &cc;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_NE(nullptr, cc.ni1);
  ASSERT_NE(nullptr, cc.ni2);
  netstack_iface_abandon(cc.ni1);
  ASSERT_EQ(0, netstack_destroy(ns));
  netstack_iface_abandon(cc.ni2); // we should still be able to use it
}
