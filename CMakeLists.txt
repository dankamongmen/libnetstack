cmake_minimum_required(VERSION 3.13)
project(libnetstack VERSION 0.0.1
  DESCRIPTION "Netlink-based networking stack model"
  HOMEPAGE_URL "https://github.com/dankamongmen/libnetstack"
  LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

file(GLOB LIBSRCS CONFIGURE_DEPENDS src/lib/*.c)
add_library(libnetstack SHARED ${LIBSRCS})
target_include_directories(libnetstack PRIVATE include)
set_target_properties(libnetstack PROPERTIES
  PUBLIC_HEADER "include/libnetstack.h"
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(libnetstack PRIVATE ${CURSES_INCLUDE_DIR})
target_link_libraries(libnetstack INTERFACE ${CURSES_LIBRARIES})
target_compile_options(libnetstack PRIVATE
  ${CURSES_CFLAGS} ${CURSES_CFLAGS_OTHER}
  -Wall -Wextra -W
)

file(GLOB BINSRCS CONFIGURE_DEPENDS src/bin/*.c)
add_executable(libnetstack-demo ${BINSRCS})
target_include_directories(libnetstack-demo PRIVATE include)
target_compile_options(libnetstack-demo PRIVATE
  -Wall -Wextra
)
target_link_libraries(libnetstack-demo libnetstack)

file(GLOB TESTSRCS CONFIGURE_DEPENDS tests/*.cpp)
add_executable(libnetstack-tester ${TESTSRCS})
find_package(GTest 1.9 REQUIRED)
target_link_libraries(libnetstack-tester
  GTest::GTest
  libnetstack
)
target_include_directories(libnetstack-tester PRIVATE include)

gtest_discover_tests(libnetstack-tester)
enable_testing()

configure_file(tools/libnetstack.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstack.pc
  @ONLY
)

include(CMakePackageConfigHelpers)

configure_package_config_file(tools/libnetstackConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstackConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/libnetstack/cmake
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstackConfigVersion.cmake
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstackConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstackConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/libnetstack
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/libnetstack.pc
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/pkgconfig
)

install(TARGETS libnetstack-demo DESTINATION bin)
install(TARGETS libnetstack
  LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT Libraries
    NAMELINK_COMPONENT Development
  PUBLIC_HEADER
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT Development
)
