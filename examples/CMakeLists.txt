cmake_minimum_required(VERSION 3.17)

set(CMAKE_CXX_STANDARD 17)

add_executable(cli cli.cpp)
target_link_libraries(cli PRIVATE tstreamer)