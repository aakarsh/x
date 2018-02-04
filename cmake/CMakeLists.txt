project(x CXX)
cmake_minimum_required(VERSION 2.6)
set (CMAKE_CXX_STANDARD 17)
add_definitions(-Wall)

find_package(Curses REQUIRED)

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CURSES_INCLUDE_DIR})

file(GLOB SRC RELATIVE "../src" "*.cc")
set(SOURCE "../src/x.cc")
add_executable(x ${SOURCE})

target_link_libraries(x ${CURSES_LIBRARIES})
