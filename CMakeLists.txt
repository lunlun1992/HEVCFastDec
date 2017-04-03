cmake_minimum_required(VERSION 3.6)
project(HEVCFastDec)

set(CMAKE_C_FLAGS "-Wall -O0")
set(SOURCE_FILES fd_hevc.c hevcfastdec.c main.c)
add_executable(HEVCFastDec ${SOURCE_FILES})