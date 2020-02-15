cmake_minimum_required(VERSION 2.8.8)

project(SimgridDagApp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

include_directories("/opt/simgrid/include")

add_executable(simulation simulation.cpp)
target_link_libraries(simulation "/opt/simgrid/lib/libsimgrid.so")
