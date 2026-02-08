cmake_minimum_required(VERSION 3.16.0 FATAL_ERROR)

add_library(czsf INTERFACE)
target_include_directories(czsf INTERFACE ${CMAKE_CURRENT_LIST_DIR})
