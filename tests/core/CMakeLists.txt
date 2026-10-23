# The test suite for core/. Included by core/CMakeLists.txt (ARP_BUILD_TESTS), so ARP_CORE_DIR,
# ARP_CORE_SOURCES and ARP_WARN come from there.
#
# Three binaries:
#   arp_tests           the suite, native __int128 where the compiler has it, plants compiled in
#   arp_tests_fallback  the SAME suite with -DARP_FORCE_INT128_FALLBACK: the whole core runs on the two-word int128
#   arp_shipped_smoke   links the shipped `arp_core` (NO ARP_PLANTS) and runs a short session
# The plant-enabled binaries compile core.cpp themselves, with ARP_PLANTS, so the shipped
# library target never contains a fault switch.
set(ARP_TEST_SOURCES test_core.cpp test_m1b.cpp test_m2a.cpp test_m2b.cpp test_int128.cpp alloc_hook.cpp)
find_package(Threads REQUIRED)   # U1 runs a real second thread against the triple buffer
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(arp_core_plants OBJECT ${ARP_CORE_SOURCES})
target_include_directories(arp_core_plants PUBLIC ${ARP_CORE_DIR}/include)
target_compile_definitions(arp_core_plants PUBLIC ARP_PLANTS)
target_compile_options(arp_core_plants PRIVATE ${ARP_WARN})

add_library(arp_core_plants_soft OBJECT ${ARP_CORE_SOURCES})
target_include_directories(arp_core_plants_soft PUBLIC ${ARP_CORE_DIR}/include)
target_compile_definitions(arp_core_plants_soft PUBLIC ARP_PLANTS ARP_FORCE_INT128_FALLBACK)
target_compile_options(arp_core_plants_soft PRIVATE ${ARP_WARN})

add_executable(arp_tests ${ARP_TEST_SOURCES} $<TARGET_OBJECTS:arp_core_plants>)
target_include_directories(arp_tests PRIVATE ${ARP_CORE_DIR}/include)
target_compile_definitions(arp_tests PRIVATE ARP_PLANTS)
target_compile_options(arp_tests PRIVATE ${ARP_WARN})
target_link_libraries(arp_tests PRIVATE Threads::Threads)

add_executable(arp_tests_fallback ${ARP_TEST_SOURCES} $<TARGET_OBJECTS:arp_core_plants_soft>)
target_include_directories(arp_tests_fallback PRIVATE ${ARP_CORE_DIR}/include)
target_compile_definitions(arp_tests_fallback PRIVATE ARP_PLANTS ARP_FORCE_INT128_FALLBACK)
target_compile_options(arp_tests_fallback PRIVATE ${ARP_WARN})
target_link_libraries(arp_tests_fallback PRIVATE Threads::Threads)

add_executable(arp_shipped_smoke shipped_smoke.cpp)
target_link_libraries(arp_shipped_smoke PRIVATE arp_core)
target_compile_options(arp_shipped_smoke PRIVATE ${ARP_WARN})

# The binaries land directly in the build dir, where tools/core_test.sh and tools/core_params_check.py look.
set_target_properties(arp_tests arp_tests_fallback arp_shipped_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_test(NAME arp_tests COMMAND arp_tests)
add_test(NAME arp_tests_fallback COMMAND arp_tests_fallback)
add_test(NAME arp_shipped_smoke COMMAND arp_shipped_smoke)
