#!/usr/bin/env python

from util import *
import sys

block_device = sys.argv[1]
data_path = 'test.ext4'
mount_path = '/media/test.ext4'

destroy_local_fs(block_device, data_path, mount_path)
