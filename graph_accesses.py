import fileinput
import re

time = 0
pos = 0
line_no = 0

time_re = re.compile(r'\s+0\.(\d+)')
lseek_re = re.compile(r'\s+0\.\d+ lseek\(\d+, (\d+), SEEK_SET\)')
read_re = re.compile(r'\s+0\.\d+ read\(.+ (\d+)\) = \d+')
pread_re = re.compile(r'\s+0\.\d+ pread\(.+ (\d+), (\d+)\) = \d+')
call_time_re = re.compile(r'<([0-9.]+)>$')

scans = []
reads = []
inode_scans = []

print 'set yrange [] reverse'
print 'unset key' # disable legend
print """set palette defined ( \
1 '#fffcf6', \
2 '#fff7db', \
3 '#fff4c2', \
4 '#feecae', \
5 '#f8ca8c', \
6 '#f0a848', \
7 '#c07860', \
8 '#a86060', \
9 '#784860', \
10 '#604860')
"""
#print 'plot "-" with vectors lc palette'
print 'plot "-" with vectors lc rgb variable'

def lseek(new_pos, call_time):
    global time
    global pos
    global line_no
    print " %d %d %d %d 0xff0000" % (pos, time, new_pos-pos, 0)
    #print " %d %d %d %d %f" % (pos, time, new_pos-pos, 1, call_time)
    pos = new_pos
    line_no += 1

def read(length, call_time):
    global time
    global pos
    global line_no
    print " %d %d %d %d 0x000000" % (pos, time, length, 0)
    #print " %d %d %d %d %f" % (pos, time, length, 0, call_time)
    pos = pos+length

for line in fileinput.input():
    match = time_re.match(line)
    if match is not None:
        time += int(match.group(1))

    match = call_time_re.search(line)
    if match is not None:
        call_time = float(match.group(1))

    match = lseek_re.match(line)
    if match is not None:
        new_pos = int(match.group(1))
        lseek(new_pos, call_time)

    match = read_re.match(line)
    if match is not None:
        length = int(match.group(1))
        read(length, call_time)

    match = pread_re.match(line)
    if match is not None:
        length = int(match.group(1))
        new_pos = int(match.group(2))
        lseek(new_pos, 0)
        read(length, call_time)

    if "BEGIN BLOCK SCAN" in line:
        scan_begin = time
    elif "END BLOCK SCAN" in line:
        scans += [(scan_begin, time)]
    elif "BEGIN BLOCK READ" in line:
        read_begin = time
    elif "END BLOCK READ" in line:
        reads += [(read_begin, time)]
    elif "BEGIN INODE SCAN" in line:
        inode_scan_begin = time
    elif "END INODE SCAN" in line:
        inode_scans += [(inode_scan_begin, time)]

print 'end'

obj_number = 1
for scan in scans:
    print "set obj %d rectangle from graph 0, first %d to graph 1, first %d" % (obj_number, scan[0], scan[1])
    print "set obj %d fc rgbcolor \"yellow\" fs solid 0.5 noborder" % (obj_number, )
    obj_number += 1

for read in reads:
    print "set obj %d rectangle from graph 0, first %d to graph 1, first %d" % (obj_number, read[0], read[1])
    print "set obj %d fc rgbcolor \"blue\" fs solid 0.25 noborder" % (obj_number, )
    obj_number += 1

for inode_scan in inode_scans:
    print "set obj %d rectangle from graph 0, first %d to graph 1, first %d" % (obj_number, inode_scan[0], inode_scan[1])
    print "set obj %d fc rgbcolor \"green\" fs solid 0.25 noborder" % (obj_number, )
    obj_number += 1
