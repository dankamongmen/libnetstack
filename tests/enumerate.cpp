#include <atomic>
#include "main.h"

// But the max must still be non-negative
TEST(Enumerate, NoNegativeMax) {
  netstack_opts nopts = { .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK, };
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  netstack_enumerator nenum{};
  uint32_t offs[1];
  char buf[1];
  int nremaining = -1;
  size_t oremaining = 1;
  ASSERT_GT(0, netstack_iface_enumerate(ns, offs, &nremaining,
                                        buf, &oremaining, &nenum));
  ASSERT_EQ(0, netstack_destroy(ns));
}

TEST(Enumerate, GetCopies) {
  netstack_opts nopts = { .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK, };
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  netstack_enumerator nenum{};
  // FIXME assumes there aren't *that* many devices on the host
  int nremaining = 256;
  size_t oremaining = 1u << 16;
  std::vector<char> buf(oremaining);
  std::vector<uint32_t> offs(nremaining);
  int enums = netstack_iface_enumerate(ns, offs.data(), &nremaining,
                                       buf.data(), &oremaining, &nenum);
  ASSERT_LT(0, enums);
  // FIXME go look at 'em?
  ASSERT_EQ(0, netstack_destroy(ns));
}
