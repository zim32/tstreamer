cmake_minimum_required(VERSION 3.17)
project(tstreamer)

set(CMAKE_CXX_STANDARD 17)

add_library(tstreamer SHARED tstreamer.cpp)
target_link_libraries(tstreamer PRIVATE torrent PUBLIC pthread)

add_subdirectory(examples)