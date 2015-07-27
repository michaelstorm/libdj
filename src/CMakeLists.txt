set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCLOG_MIN_LOG_LEVEL=${MIN_LOG_LEVEL} -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 --std=c99 -fdump-rtl-expand")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -ggdb")

set(DJ_LIBRARY_SOURCE dj.c heap.c md5.c util.c block_scan.c dir_scan.c dj_util.c stripe.c)
include_directories(logger)
add_subdirectory(logger)

add_library(dj SHARED ${DJ_LIBRARY_SOURCE})
target_link_libraries(dj ext2fs crypto rt com_err)

add_executable(dj_cmd cmd_line.c)
target_link_libraries(dj_cmd dj)

add_executable(read_dir_files read_dir_files.c)

add_executable(vmtouch vmtouch.c)
