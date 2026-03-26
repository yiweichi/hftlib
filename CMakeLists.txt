cmake_minimum_required(VERSION 3.16)
project(hftlib-practice LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Aggressive optimizations for release builds
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

include_directories(include)

add_executable(test_simple_vector tests/test_simple_vector.cpp)
add_executable(test_smart_ptr     tests/test_smart_ptr.cpp)
add_executable(test_spsc_ring_buffer tests/test_spsc_ring_buffer.cpp)

target_link_libraries(test_spsc_ring_buffer PRIVATE pthread)

enable_testing()
add_test(NAME SimpleVector   COMMAND test_simple_vector)
add_test(NAME SmartPtr        COMMAND test_smart_ptr)
add_test(NAME SPSCRingBuffer  COMMAND test_spsc_ring_buffer)
