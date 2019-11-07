#include <mutex>
#include "main.h"

// Unit tests for running iface inspection from within callbacks

// Sets up the callback subject interface to be copied by the callback itself,
// then handed to an external thread.
static void
InspectCB(const netstack_iface* ni, netstack_event_e etype,
          void* curry __attribute__ ((unused))) {
  if(etype != NETSTACK_MOD){
    return;
  }
  struct rtnl_link_stats stats;
  ASSERT_TRUE(netstack_iface_stats(ni, &stats));
}

// Test deep copying of an interface from within its callback context
TEST(IfaceCB, CheckAttributes) {
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  nopts.initial_events = NETSTACK_INITIAL_EVENTS_BLOCK;
  nopts.iface_cb = InspectCB;
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_EQ(0, netstack_destroy(ns));
}
