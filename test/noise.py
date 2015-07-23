#!/usr/bin/env python

from util import *
import sys

block_device = sys.argv[1] #run_command('losetup -f').std_out.strip()
data_path = 'test.ext4'
mount_path = '/media/test.ext4'

try:
    #create_local_fs(1000000, block_device, data_path, mount_path)
    create_fs(block_device, mount_path)

    print("Filling disk...")
    for i in range(10):
        target_usage_ratio = random.uniform(.95, 1)
        print("Ratio:", target_usage_ratio)
        write_until_usage(block_device, 0, 100 * MEGABYTE, target_usage_ratio)

    # sleep to ensure disk is not busy when we try to unmount it
    time.sleep(1)

    # remount disk because the powers of fsync seem to be useless against loopback devices
    run_command('umount "%s"' % mount_path)
    run_command('mount %s "%s"' % (block_device, mount_path))

    for i in range(10):
        start = time.time()
        run_command_streaming_stdout(['../build/src/dj_cmd', '-direct', block_device, '/'])
        end = time.time()
        print(end - start)

finally:
    # sleep to ensure disk is not busy when we try to unmount it
    time.sleep(1)
    #destroy_local_fs(block_device, data_path, mount_path)
