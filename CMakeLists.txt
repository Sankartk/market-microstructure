cmake_minimum_required(VERSION 3.20)
project(market-microstructure VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

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
