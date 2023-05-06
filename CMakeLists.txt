cmake_minimum_required(VERSION 3.13)
project(netstack VERSION 0.7.2
  DESCRIPTION "Netlink-based networking stack model"
  HOMEPAGE_URL "https://github.com/dankamongmen/netstack"
  LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NL3 REQUIRED libnl-3.0>=3.4.0)

file(GLOB LIBSRCS CONFIGURE_DEPENDS src/lib/*.c)
add_library(netstack SHARED ${LIBSRCS})
target_include_directories(netstack PRIVATE include)
set_target_properties(netstack PROPERTIES
  PUBLIC_HEADER "include/netstack.h"
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_include_directories(netstack PRIVATE ${NL3_INCLUDE_DIR})
target_link_libraries(netstack
  PRIVATE
    ${NL3_LIBRARIES}
  INTERFACE
	  Threads::Threads
)
target_compile_options(netstack
  PRIVATE
    ${NL3_CFLAGS} ${NL3_CFLAGS_OTHER}
    -Wall -Wextra -W -Wshadow
    -D_POSIX_C_SOURCE=200809L # for strndup()
)
target_compile_definitions(netstack
  PRIVATE
   _DEFAULT_SOURCE
)

add_library(netstack-static STATIC ${LIBSRCS})
target_include_directories(netstack-static PRIVATE include)
set_target_properties(netstack-static PROPERTIES
  PUBLIC_HEADER "include/netstack.h"
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  OUTPUT_NAME netstack
)
target_include_directories(netstack-static PRIVATE ${NL3_INCLUDE_DIR})
target_link_libraries(netstack-static
  PRIVATE
    ${NL3_LIBRARIES}
  INTERFACE
	  Threads::Threads
)
target_compile_options(netstack-static PRIVATE
  ${NL3_CFLAGS} ${NL3_CFLAGS_OTHER}
  -Wall -Wextra -W -Wshadow
)
file(GLOB BINSRCS CONFIGURE_DEPENDS src/bin/*.c)
add_executable(netstack-demo ${BINSRCS})
target_include_directories(netstack-demo PRIVATE include)
target_compile_options(netstack-demo PRIVATE
  -Wall -Wextra -Wshadow
)
target_link_libraries(netstack-demo netstack)

file(GLOB TESTSRCS CONFIGURE_DEPENDS tests/*.cpp)
add_executable(netstack-tester ${TESTSRCS})
find_package(GTest 1.9 REQUIRED)
target_link_libraries(netstack-tester
  GTest::GTest
  netstack
)
target_compile_options(netstack-tester PRIVATE
  -Wall -Wextra -Wshadow
)
target_include_directories(netstack-tester PRIVATE include)

gtest_discover_tests(netstack-tester)
enable_testing()

configure_file(tools/libnetstack.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstack.pc
  @ONLY
)

include(CMakePackageConfigHelpers)

configure_package_config_file(tools/libnetstackConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstackConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/netstack/cmake
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstackConfigVersion.cmake
  COMPATIBILITY SameMajorVersion
)

# FIXME i think we can replace this with an install(EXPORT)?
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstackConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstackConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libnetstack
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstack.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

install(TARGETS netstack netstack-demo netstack-static
  RUNTIME
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Runtime
  LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT Libraries
    NAMELINK_COMPONENT Development
  PUBLIC_HEADER
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT Development
  ARCHIVE
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT Libraries
)
