cmake_minimum_required(VERSION 3.17)
project(tstreamer)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(vendor/fmt)

add_library(tstreamer SHARED tstreamer.cpp)
target_link_libraries(tstreamer PUBLIC torrent pthread fmt::fmt-header-only)
target_include_directories(tstreamer PUBLIC vendor/fmt/include)

add_subdirectory(examples)