cmake_minimum_required(VERSION 3.6)
project(HEVCFastDec)


set(SOURCE_FILES fd_hevc.c hevcfastdec.c main.c)
add_executable(HEVCFastDec ${SOURCE_FILES})