cmake_minimum_required(VERSION 3.16)
project(nrfp_host C)
enable_testing()

add_subdirectory(libnrfp)
add_subdirectory(daemon)
add_subdirectory(tools)
