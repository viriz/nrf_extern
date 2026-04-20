cmake_minimum_required(VERSION 3.16)
project(nrfp_zynq_user C)

add_executable(nrfp_zynq_ping demo_ping.c)
target_include_directories(nrfp_zynq_ping PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include)
