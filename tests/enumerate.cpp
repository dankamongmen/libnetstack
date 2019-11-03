#include <atomic>
#include "main.h"

// with NETSTACK_ENUMERATE_ABORT, we needn't set buffer arguments
TEST(Enumerate, AbortValidAlone) {
  netstack_opts nopts = { .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK, };
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  struct netstack_enumerator* nenum = nullptr;
  ASSERT_EQ(0, netstack_iface_enumerate(ns, nullptr, 0, nullptr, 0,
                                        NETSTACK_ENUMERATE_ABORT, &nenum));
  ASSERT_EQ(0, netstack_destroy(ns));
}

// But the max must still be non-negative
TEST(Enumerate, AbortNoNegativeMax){
  netstack_opts nopts = { .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK, };
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  struct netstack_enumerator* nenum = nullptr;
  ASSERT_GT(0, netstack_iface_enumerate(ns, nullptr, -1, nullptr, 0,
                                        NETSTACK_ENUMERATE_ABORT, &nenum));
  ASSERT_EQ(0, netstack_destroy(ns));
}

// NETSTACK_ENUMERATE_ABORT cannot be used with other flags
TEST(Enumerate, AbortNoOtherFlags){
  netstack_opts nopts = { .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK, };
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  struct netstack_enumerator* nenum = nullptr;
  for(unsigned i = 1 ; i ; i <<= 1){
    if(i == NETSTACK_ENUMERATE_ABORT){
      continue;
    }
    ASSERT_GT(0, netstack_iface_enumerate(ns, nullptr, 0, nullptr, 0,
                                          i | NETSTACK_ENUMERATE_ABORT, &nenum));
  } 
  ASSERT_EQ(0, netstack_destroy(ns));
}

// NETSTACK_ENUMERATE_ATOMIC ought fail with too small a buffer
TEST(Enumerate, AtomicRequiresSpace){
  netstack_opts nopts = { .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK, };
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  struct netstack_enumerator* nenum = nullptr;
  char buf[1];
  uint32_t offs[1];
  ASSERT_GT(0, netstack_iface_enumerate(ns, offs, 1, buf, 1,
                                        NETSTACK_ENUMERATE_ATOMIC, &nenum));
  ASSERT_EQ(0, netstack_destroy(ns));
}
