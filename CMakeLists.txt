cmake_minimum_required(VERSION 3.20)

project(BinFlow LANGUAGES CXX)

add_library(binflow INTERFACE)
target_include_directories(binflow INTERFACE include)
target_compile_features(binflow INTERFACE cxx_std_20)

add_executable(binflow_basic samples/basic.cpp)
target_link_libraries(binflow_basic PRIVATE binflow)
