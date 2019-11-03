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
  int wantn;
  int wantbytes;
  int nremaining = 256;
  size_t oremaining = 1u << 16;
  std::vector<char> buf(oremaining);
  std::vector<uint32_t> offs(nremaining);
  do{
    int enums = netstack_iface_enumerate(ns, offs.data(), &nremaining,
                                         buf.data(), &oremaining, &nenum);
    ASSERT_LT(0, enums);
    for(int z = 0 ; z < enums ; ++z){
      const netstack_iface* ni =
          reinterpret_cast<struct netstack_iface*>(buf.data() + offs[z]);
      EXPECT_NE(0, netstack_iface_index(ni));
    }
    wantn = nremaining;
    wantbytes = oremaining;
    nremaining = offs.capacity();
    oremaining = buf.capacity();
  }while(wantn || wantbytes);
  ASSERT_EQ(0, netstack_destroy(ns));
}

TEST(Enumerate, OneByOne) {
  netstack_opts nopts = { .initial_events = NETSTACK_INITIAL_EVENTS_BLOCK, };
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  netstack_enumerator nenum{};
  int wantn;
  int wantbytes;
  int nremaining = 1;
  size_t oremaining = 1u << 16;
  std::vector<char> buf(oremaining);
  std::vector<uint32_t> offs(1);
  do{
    int enums = netstack_iface_enumerate(ns, offs.data(), &nremaining,
                                         buf.data(), &oremaining, &nenum);
    ASSERT_LT(0, enums);
    for(int z = 0 ; z < enums ; ++z){
      const netstack_iface* ni =
          reinterpret_cast<struct netstack_iface*>(buf.data() + offs[z]);
      EXPECT_NE(0, netstack_iface_index(ni));
    }
    wantn = nremaining;
    wantbytes = oremaining;
    nremaining = offs.capacity();
    oremaining = buf.capacity();
  }while(wantn || wantbytes);
  ASSERT_EQ(0, netstack_destroy(ns));
}
