# The test suite for core/. Included by core/CMakeLists.txt (ARP_BUILD_TESTS), so ARP_CORE_DIR,
# ARP_CORE_SOURCES and ARP_WARN come from there.
#
# Three binaries:
#   arp_tests           the suite, native __int128 where the compiler has it, plants compiled in
#   arp_tests_fallback  the SAME suite with -DARP_FORCE_INT128_FALLBACK: the whole core runs on the two-word int128
#   arp_shipped_smoke   links the shipped `arp_core` (NO ARP_PLANTS) and runs a short session
# The plant-enabled binaries compile core.cpp themselves, with ARP_PLANTS, so the shipped
# library target never contains a fault switch.
set(ARP_TEST_SOURCES test_core.cpp test_m1b.cpp test_m2a.cpp test_m2b.cpp test_m3.cpp test_random.cpp test_randomize.cpp test_evict.cpp test_input.cpp digest_corpus.cpp test_digest.cpp test_int128.cpp alloc_hook.cpp)
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

# The committed stream digest (test_digest.cpp): the suite's output, hashed, from the pre-optimisation core (D-030).
set(ARP_DIGEST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/stream_digest.txt)
set(ARP_RANDOM_DIGEST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/stream_digest_random.txt)
set(ARP_RANDOMIZE_DIGEST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/randomize_digest.txt)   # SPEC 15 RZ1: the randomize digest (test_randomize.cpp), identical on every platform   # D-036: the randomness suite's own digest (test_random.cpp)
add_executable(arp_tests ${ARP_TEST_SOURCES} $<TARGET_OBJECTS:arp_core_plants>)
target_include_directories(arp_tests PRIVATE ${ARP_CORE_DIR}/include)
target_compile_definitions(arp_tests PRIVATE ARP_PLANTS ARP_DIGEST_FILE="${ARP_DIGEST_FILE}" ARP_RANDOM_DIGEST_FILE="${ARP_RANDOM_DIGEST_FILE}" ARP_RANDOMIZE_DIGEST_FILE="${ARP_RANDOMIZE_DIGEST_FILE}")
target_compile_options(arp_tests PRIVATE ${ARP_WARN})
target_link_libraries(arp_tests PRIVATE Threads::Threads)

add_executable(arp_tests_fallback ${ARP_TEST_SOURCES} $<TARGET_OBJECTS:arp_core_plants_soft>)
target_include_directories(arp_tests_fallback PRIVATE ${ARP_CORE_DIR}/include)
target_compile_definitions(arp_tests_fallback PRIVATE ARP_PLANTS ARP_FORCE_INT128_FALLBACK ARP_DIGEST_FILE="${ARP_DIGEST_FILE}" ARP_RANDOM_DIGEST_FILE="${ARP_RANDOM_DIGEST_FILE}" ARP_RANDOMIZE_DIGEST_FILE="${ARP_RANDOMIZE_DIGEST_FILE}")
target_compile_options(arp_tests_fallback PRIVATE ${ARP_WARN})
target_link_libraries(arp_tests_fallback PRIVATE Threads::Threads)

add_executable(arp_shipped_smoke shipped_smoke.cpp)
target_link_libraries(arp_shipped_smoke PRIVATE arp_core)
target_compile_options(arp_shipped_smoke PRIVATE ${ARP_WARN})

# The benchmark (M3a, T-007): the SHIPPED core (no ARP_PLANTS) once per int128 path. Reported, never gated, never run by ./verify;
# tools/core_bench.sh builds these two with absolute paths in a Release tree of their own and runs them. Compiled here too, so a
# change to the core that breaks the benchmark fails tools/core_test.sh instead of rotting.
add_library(arp_core_soft STATIC ${ARP_CORE_SOURCES})
target_include_directories(arp_core_soft PUBLIC ${ARP_CORE_DIR}/include)
target_compile_features(arp_core_soft PUBLIC cxx_std_20)
target_compile_definitions(arp_core_soft PUBLIC ARP_FORCE_INT128_FALLBACK)
set_target_properties(arp_core_soft PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(arp_core_soft PRIVATE ${ARP_WARN})
add_executable(arp_bench bench.cpp alloc_hook.cpp)
target_link_libraries(arp_bench PRIVATE arp_core)
target_compile_options(arp_bench PRIVATE ${ARP_WARN})
add_executable(arp_bench_soft bench.cpp alloc_hook.cpp)
target_link_libraries(arp_bench_soft PRIVATE arp_core_soft)
target_compile_options(arp_bench_soft PRIVATE ${ARP_WARN})

# The listening renders (M3 gate, the human's listening sign-off): drives the SHIPPED core (no ARP_PLANTS) with scripted hosts and writes
# WAV / MIDI / manifest.json for lab/listen.html. tools/render_listening.sh builds and runs it; ./verify never does. alloc_hook.cpp is linked so the
# renderer can prove nothing allocates inside process(). Compiled by tools/core_test.sh too, so a core change that breaks it fails there.
add_executable(arp_render render.cpp alloc_hook.cpp)
target_link_libraries(arp_render PRIVATE arp_core)
target_compile_options(arp_render PRIVATE ${ARP_WARN})

# The binaries land directly in the build dir, where tools/core_test.sh and tools/core_params_check.py look.
set_target_properties(arp_tests arp_tests_fallback arp_shipped_smoke arp_bench arp_bench_soft arp_render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_test(NAME arp_tests COMMAND arp_tests)
add_test(NAME arp_tests_fallback COMMAND arp_tests_fallback)
add_test(NAME arp_shipped_smoke COMMAND arp_shipped_smoke)
