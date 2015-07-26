#!/usr/bin/env python

import envoy
import os.path
import random
import subprocess
import sys
import time
import uuid

MEGABYTE = 1048576


def run_command(command, print_debug=True, check_error=True):
    if print_debug:
        sys.stdout.write('Running %s... ' % command)
        sys.stdout.flush()

    r = envoy.run(command)
    if check_error and r.status_code != 0:
        raise Exception('Error running "%s": exited with code %d, error: %s', command, r.status_code, r.std_err)

    if print_debug:
        if r.status_code == 0:
            sys.stdout.write('succeeded\n')
        else:
            sys.stdout.write('failed\n')
        sys.stdout.flush()

    return r


def run_commands(commands, **kwargs):
    for command in commands:
        run_command(command, **kwargs)


def create_local_fs(size_kb, block_device, data_path, mount_path):
    dd_cmd = 'dd if=/dev/zero of="%s" bs=1K count=%d' % (data_path, size_kb)
    losetup_cmd = 'losetup %s %s' % (block_device, data_path)
    mkfs_cmd = 'mkfs.ext4 %s' % block_device
    mkdir_cmd = 'mkdir -p %s' % mount_path
    mount_cmd = 'mount %s "%s"' % (block_device, mount_path)
    commands = [dd_cmd, losetup_cmd, mkfs_cmd, mkdir_cmd, mount_cmd]
    run_commands(commands)


def create_fs(block_device, mount_path):
    mkfs_cmd = 'mkfs.ext4 -F %s' % block_device
    mkdir_cmd = 'mkdir -p %s' % mount_path
    mount_cmd = 'mount %s "%s"' % (block_device, mount_path)
    commands = [mkfs_cmd, mkdir_cmd, mount_cmd]
    run_commands(commands)


def destroy_local_fs(block_device, data_path, mount_path):
    mount_cmd = 'umount "%s"' % mount_path
    losetup_cmd = 'losetup -d %s' % block_device
    rm_cmd = 'rm %s' % data_path
    commands = [mount_cmd, losetup_cmd, rm_cmd]
    run_commands(commands, check_error=False)


def get_files(root_path):
    files = None
    for _, _, f in os.walk(root_path):
        files = f
        break

    return files


def write_file(root_path, size):
    path = os.path.join(root_path, str(uuid.uuid4()))
    if size < MEGABYTE:
        run_command('dd if=/dev/urandom of=%s bs=1K count=%s' % (path, int(size/1024)))
    else:
        run_command('dd if=/dev/urandom of=%s bs=1M count=%s' % (path, int(size/MEGABYTE)))
    #with open(path, 'w') as f:
    #    #print('Writing file %s of %d bytes' % (path, size))
    #    f.write(''.join(random.choice('0123456789ABCDEF') for i in range(size)))


def write_random_files(root_path, min_bytes, max_bytes, count):
    for i in range(count):
        size = random.randint(min_bytes, max_bytes)
        write_file(root_path, size)


def delete_random_file(root_path):
    files = get_files(root_path)

    if len(files) > 0:
        filename = random.choice(files)
        path = os.path.join(root_path, filename)

        #print('Deleting %s' % path)
        os.remove(path)
        return True
    else:
        print('No files to delete')
        return False


def write_until_usage(block_device, min_file_bytes, max_file_bytes, target_usage_ratio):
    fs_info = get_fs_info(block_device)
    usage = fs_info[0] * 1024
    available = fs_info[1] * 1024
    root_path = fs_info[2]

    total = usage + available
    target_usage = int(total * target_usage_ratio)

    if usage < target_usage:
        while usage < target_usage - min_file_bytes:
            print('total: %d, target_usage: %d, current: %d; writing' % (total, target_usage, usage))
            file_bytes_ceiling = int(max_file_bytes/100 if random.random() < .5 else max_file_bytes)
            file_size = random.randint(min_file_bytes, min(file_bytes_ceiling, target_usage - usage))
            write_file(root_path, file_size)
            usage = get_fs_info(block_device)[0] * 1024
    else:
        while usage > target_usage + min_file_bytes:
            print('total: %d, target_usage: %d, current: %d; deleting' % (total, target_usage, usage))
            deleted = delete_random_file(root_path)
            usage = get_fs_info(block_device)[0] * 1024
            if not deleted:
                break

    #print('Ending with differential of %d bytes' % (target_usage - usage))


def get_fs_info(block_device):
    r = run_command('df %s' % block_device, print_debug=False)
    line = r.std_out.split('\n')[1].split()
    return int(line[2]), int(line[3]), line[5]


def run_command_streaming_stdout(args):
    def print_line(line):
        sys.stdout.write(line.decode('ascii'))
        sys.stdout.flush()

    p = subprocess.Popen(args, stdout=subprocess.PIPE)

    while p.poll() is None:
        line = p.stdout.readline()
        print_line(line)

    line = p.stdout.read()
    print_line(line)
