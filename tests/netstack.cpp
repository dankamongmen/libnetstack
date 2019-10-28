#include "main.h"

TEST(Netstack, CreateNULLOpts){
  struct netstack* ns = netstack_create(NULL);
  ASSERT_NE(nullptr, ns);
  ASSERT_EQ(0, netstack_destroy(ns));
}

TEST(Netstack, CreateDefaultOpts){
  netstack_opts nopts;
  memset(&nopts, 0, sizeof(nopts));
  struct netstack* ns = netstack_create(&nopts);
  ASSERT_NE(nullptr, ns);
  ASSERT_EQ(0, netstack_destroy(ns));
}
