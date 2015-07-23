#!/usr/bin/env python

from util import *
import sys

block_device = sys.argv[1]
data_path = 'test.ext4'
mount_path = '/media/test.ext4'

for i in range(1):
    start = time.time()
    run_command_streaming_stdout(['../build/src/dj_cmd', '-direct', '-cat', block_device, '/'])
    end = time.time()
    print(end - start)
