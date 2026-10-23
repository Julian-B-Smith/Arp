# Arp core - the note processor (SPEC.md). Self-contained and framework-free: C++20, standard
# library only, no dependency, buildable without horde (D-015: horde copies core/include + core/src
# in at our V1 and records our commit; or add_subdirectory()s this directory, which then builds only
# the library). The shippable files are exactly: include/arp/*.h and src/core.cpp.
#
# Configure on this Mac with the Command Line Tools only:
#   cmake -S <repo>/core -B <repo>/build/core -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
# (tools/core_test.sh does exactly that, with absolute paths.)
cmake_minimum_required(VERSION 3.16)
project(arp_core LANGUAGES CXX)

# The test suite (../tests/core) is built only when this is the top-level project, so a consumer that
# add_subdirectory()s the core never needs the tests directory.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ARP_IS_TOP_LEVEL ON)
else()
  set(ARP_IS_TOP_LEVEL OFF)
endif()
option(ARP_WERROR "Treat warnings as errors" OFF)
option(ARP_BUILD_TESTS "Build the test suite (tests/core)" ${ARP_IS_TOP_LEVEL})

if(MSVC)
  set(ARP_WARN /W4)
  if(ARP_WERROR)
    list(APPEND ARP_WARN /WX)
  endif()
else()
  set(ARP_WARN -Wall -Wextra)
  if(ARP_WERROR)
    list(APPEND ARP_WARN -Werror)
  endif()
endif()

set(ARP_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ARP_CORE_SOURCES ${ARP_CORE_DIR}/src/core.cpp)

# The SHIPPED library: no ARP_PLANTS, so none of the test-only fault switches exist in it.
add_library(arp_core STATIC ${ARP_CORE_SOURCES})
target_include_directories(arp_core PUBLIC ${ARP_CORE_DIR}/include)
target_compile_features(arp_core PUBLIC cxx_std_20)
set_target_properties(arp_core PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(arp_core PRIVATE ${ARP_WARN})

if(ARP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(${ARP_CORE_DIR}/../tests/core ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
