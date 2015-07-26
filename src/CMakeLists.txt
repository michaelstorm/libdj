set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCLOG_MIN_LOG_LEVEL=${MIN_LOG_LEVEL} --std=c99 -fdump-rtl-expand")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -ggdb")

# fixes a bunch of time.h stuff
add_definitions(-D_POSIX_C_SOURCE=200112L)

set(DJ_LIBRARY_SOURCE dj.c heap.c listsort.c md5.c util.c block_scan.c dir_scan.c dj_util.c)
include_directories(logger)
add_subdirectory(logger)

add_library(dj SHARED ${DJ_LIBRARY_SOURCE})
target_link_libraries(dj ext2fs crypto rt com_err)

add_executable(dj_cmd cmd_line.c)
target_link_libraries(dj_cmd dj)

add_executable(profile profile.c)
target_link_libraries(profile dj)

add_executable(read_dir_files read_dir_files.c)

add_executable(vmtouch vmtouch.c)

add_executable(lister lister.c)
target_link_libraries(lister ext2fs com_err)
