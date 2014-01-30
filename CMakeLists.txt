cmake_minimum_required(VERSION 2.8)
project(dj)

set(CMAKE_C_FLAGS --std=c99)

add_library(dj SHARED dj.c heap.c listsort.c md5.c crc.c)
target_link_libraries(dj ext2fs crypto rt)

add_executable(dj_cmd cmd_line.c)
target_link_libraries(dj_cmd dj)

add_executable(profile profile.c)
target_link_libraries(profile dj)

add_executable(read_dir_files read_dir_files.c)

add_executable(vmtouch vmtouch.c)

add_executable(lister lister.c)
target_link_libraries(lister ext2fs)
