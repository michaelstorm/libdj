#!/usr/bin/env python

from util import *
import sys

block_device = sys.argv[1] # run_command('losetup -f').std_out.strip()
data_path = 'test.ext4'
mount_path = '/media/test.ext4'

print(block_device)

#create_local_fs(10000, block_device, data_path, mount_path)

for i in range(10000):
    target_usage_ratio = random.uniform(.95, 1)
    print("Iteration:", i)
    print("Ratio:", target_usage_ratio)
    write_until_usage(block_device, 0, 100 * MEGABYTE, target_usage_ratio)
