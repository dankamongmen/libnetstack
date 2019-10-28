#ifndef LIBNETSTACK_TEST_MAIN
#define LIBNETSTACK_TEST_MAIN

#include <gtest/gtest.h>
#include <netstack.h>

// GTEST_SKIP only came along in GoogleTest 1.9
#ifndef GTEST_SKIP
#define GTEST_SKIP() return;
#endif

#endif
