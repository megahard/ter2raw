cmake_minimum_required(VERSION 3.6)
project(ter2raw)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

include_directories (/usr/local/include)
link_directories (/usr/local/lib)
link_libraries (png16)

set(SOURCE_FILES main.cpp)
add_executable(ter2raw ${SOURCE_FILES})