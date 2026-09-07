cmake_minimum_required(VERSION 3.20)
project(market-microstructure VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

find_package(Threads REQUIRED)

add_executable(microstructure
  src/main.cpp
  src/order_book.cpp
  src/itch_parser.cpp
  src/detector.cpp
  src/feed_handler.cpp
)

target_include_directories(microstructure PRIVATE include)
target_link_libraries(microstructure PRIVATE Threads::Threads)

# Benchmark binary
add_executable(benchmark
  bench/benchmark.cpp
  src/order_book.cpp
  src/itch_parser.cpp
)
target_include_directories(benchmark PRIVATE include)

# Tests
enable_testing()

add_executable(test_order_book tests/test_order_book.cpp src/order_book.cpp)
target_include_directories(test_order_book PRIVATE include)

add_executable(test_itch_parser tests/test_itch_parser.cpp src/itch_parser.cpp)
target_include_directories(test_itch_parser PRIVATE include)

add_executable(test_detector tests/test_detector.cpp src/detector.cpp src/order_book.cpp)
target_include_directories(test_detector PRIVATE include)

add_test(NAME order_book  COMMAND test_order_book)
add_test(NAME itch_parser COMMAND test_itch_parser)
add_test(NAME detector    COMMAND test_detector)
