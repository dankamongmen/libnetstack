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
TEST(Enumerate, AbortNoNegativeMax) {
  netstack_opts nopts = { .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK, };
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  struct netstack_enumerator* nenum = nullptr;
  ASSERT_GT(0, netstack_iface_enumerate(ns, nullptr, -1, nullptr, 0,
                                        NETSTACK_ENUMERATE_ABORT, &nenum));
  ASSERT_EQ(0, netstack_destroy(ns));
}

// NETSTACK_ENUMERATE_ABORT cannot be used with other flags
TEST(Enumerate, AbortNoOtherFlags) {
  netstack_opts nopts = { .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK, };
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  struct netstack_enumerator* nenum = nullptr;
  for(unsigned i = 1 ; i ; i <<= 1) {
    if(i == NETSTACK_ENUMERATE_ABORT) {
      continue;
    }
    ASSERT_GT(0, netstack_iface_enumerate(ns, nullptr, 0, nullptr, 0,
                                          i | NETSTACK_ENUMERATE_ABORT, &nenum));
  } 
  ASSERT_EQ(0, netstack_destroy(ns));
}

// NETSTACK_ENUMERATE_ATOMIC ought fail with too small a buffer (assumes device FIXME)
TEST(Enumerate, AtomicRequiresSpace) {
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

TEST(Enumerate, GetCopies) {
  netstack_opts nopts = { .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK, };
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  struct netstack_enumerator* nenum = nullptr;
  // FIXME assumes there aren't *that* many devices on the host
  std::vector<char> buf(1u << 16);
  std::vector<uint32_t> offs(256);
  int enums = netstack_iface_enumerate(ns, offs.data(), offs.capacity() * sizeof(uint32_t),
                                       buf.data(), buf.capacity(), 0, &nenum);
  ASSERT_LT(0, enums);
  ASSERT_EQ(0, netstack_destroy(ns));
}

// you *should not* call netstack_iface_abandon() on enumerated results (you
// provided the resources, after all). despite that, we try not to crash or
// otherwise blow up if you do.
TEST(Enumerate, ErroneouslyAbandon) {
  netstack_opts nopts = { .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK, };
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  struct netstack_enumerator* nenum = nullptr;
  // FIXME assumes there aren't *that* many devices on the host
  std::vector<char> buf(1u << 16);
  std::vector<uint32_t> offs(256);
  int enums = netstack_iface_enumerate(ns, offs.data(), offs.capacity() * sizeof(uint32_t),
                                       buf.data(), buf.capacity(), 0, &nenum);
  ASSERT_LT(0, enums);
  ASSERT_EQ(0, netstack_destroy(ns));
  // DO NOT COPY-AND-PASTE
  // ABANDON ALL HOPE YE WHO CALL NETSTACK_IFACE_ABANDON HERE
  // IT IS NOT A SMART THING TO DO, AND WORKS ONLY ACCIDENT
  // I JUST WANT TO KNOW IF IT STARTS TO BREAK
  for(int z = 0 ; z < enums ; ++z){
    netstack_iface_abandon(reinterpret_cast<struct netstack_iface*>(buf.data() + offs[z]));
  }
  // DO NOT COPY-AND-PASTE
}
