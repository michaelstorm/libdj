# libdj

An implementation of
[this excellent paper](http://www.ecsl.cs.sunysb.edu/tr/TR240.pdf) on disk
geometry-aware file system access for ext2 and later.

## What's it do?

This library reads data from a disk drive faster than most operating systems do.
As the amount of data to be read or the fragmentation level of the file system
increases, so does the efficiency advantage of this library. (Please, nobody
post this a news site just yet, or they'll eat me alive for lack of numbers to
back up that claim.) However, it requires more memory than most OSs' naive
strategies do in order to realize these gains.

In a nutshell, ext2 and later (like most file systems) try to lay out files as
contiguous stretches of blocks. However, due to fragmentation, they frequently
must write blocks to disparate stretches of disk. What's more, many applications
such as anti-virus scanners, backup software, and disk consistency checkers read
a vast majority of the disk all at once, but have to read through the disk
file-by-file. Thus, they cannot take advantage of the fact that, while
individual files may be fragmented, a whole group of files they're interested in
may be contiguous.

A few examples should clarify what I'm talking about. Here's a perfect-world
scenario in which a file is laid out completely contiguously on disk:

                A0-A12
    ............<XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX>............
    0   1   2   3   4   5   6   7   8   9   10  11  12  13  14  15  16  17  18

Blocks 0-12 of file A are laid out at physical blocks 3-15 on disk. The file
system can read the entire file by issuing the (hypothetical) instructions:

    SEEK 3
    READ 12

where 3 is the starting location, and 12 is the number of blocks to be read.

Real life is hard, though. In real life, we might see the file laid out like
this:

        A0      A1-3        A9-12                   A4-8
    ....<XX>....<XXXXXXXXXX><XXXXXXXXXXXXXX>........<XXXXXXXXXXXXXXXXXXX>.......
    0   1   2   3   4   5   6   7   8   9   10  11  12  13  14  15  16  17  18

A naive strategy (i.e., most operating systems) would issue instructions like:

    SEEK 1
    READ 1
    SEEK 1
    READ 3
    SEEK 6
    READ 5
    SEEK -11
    READ 4

That's four seeks to read the same data as above. But can we do better? We can't
change the way the file is laid out on disk, but we can change our read
strategy. We notice that blocks 9-12 are right next to blocks 1-3 on disk. What
if we read blocks 1-3 and 9-12 from physical positions 3-9 all in one go?

    SEEK 1
    READ 1
    SEEK 1
    READ 7
    SEEK 2
    READ 5

That's only 3 seeks. But now, we have to keep blocks 9-12 in memory until we've
read 4-8, if we want to deliver them to our client in logical order. We've
arrived at the crucial trade-off: with more memory available to the reader, we
can buffer more out-of-order blocks. If we run out of that scratch memory space,
the reader must switch to reading blocks naively (in logical order), until the
out-of-order blocks in memory can be flushed out of memory.

The example above is a little contrived. How about a more real-world example?

Let's say that there are three files, A, B, and C, written to disk like this:

    B0-1    C0  A0-4            B2-4                C1-6                A5-6
    <XXXXXX><XX><XXXXXXXXXXXXXX><XXXXXXXXXX>........<XXXXXXXXXXXXXXXXXX><XXXXXX>
    0   1   2   3   4   5   6   7   8   9   10  11  12  13  14  15  16  17  18

Let's say some backup software wants to read files A, B, and C (and let's say in
that order). To do so naively would require the instructions:

    # read A
    SEEK 3
    READ 4
    SEEK 10
    READ 2

    # read B
    SEEK -18
    READ 2
    SEEK 5
    READ 3

    # read C
    SEEK -8
    READ 1
    SEEK 9
    READ 5

Let's improve on that. There's a 10-block section (the code calls these
"stripes") of contiguous blocks that the client wants, but that we read *four
times*. The second stripe, from physical positions 12-18, is read twice. But
using our new strategy, we can issue the instructions:

    READ 10
    SEEK 2
    READ 6

and we're done.

## Inode sorting

(The following is an explanation of why we sort a list of inodes at one point
in the code. I pulled it out here because it was taking up too much space as a
comment.)

You may ask yourself, "Why sort on the inode numbers at all, if what we really
care about is the order of the data blocks?" My friend, you ask a reasonable
question. Consider the following.

1. We can only keep a certain number of blocks in memory at a time.
2. We can only keep a certain number of files in memory at a time. This is
mostly due to clients that maintain state on a per-file basis, which is an
entirely reasonable thing to do. If we feed them too many files at once, they
blow out all their memory.
3. Therefore, we have to choose which files to buffer in memory at any one time.
We can do this:
  1. intelligently, by iterating through the files' metadata and figuring out
  which sets of files have the most-contiguous blocks, or
  2. stupidly, by sorting the files on their inode numbers and working our way
  through them, on the assumption that the blocks of close-together inodes will
  be close together themselves.

I have chosen option (2) of point (3), for the moment. You may ask yourself,
"Doesn't that merely punt on the problem we're trying to solve? This whole
exercise about sorting blocks is predicated on the file system not allocating
blocks perfectly efficiently, but now we're going to assume that the ordering of
inodes is reasonably close to the ordering of their blocks?"

Yes. Because I'm too lazy at the moment to do any better (yay, open source),
and because a perfect implementation of option (i) doesn't exist in the general
case, *assuming* that such an implementation requires non-constant (and
non-trivial) memory asymptotically. In other words, if we have a really low
constraint on available memory and a really large file system to examine, there
ain't no way to pick sets of files with mostly-contiguous blocks without eating
up all your memory tracking where those blocks are. At least, I think this is
the case. (Edit: Maybe you could do it fuzzily, and dial up the fuzziness as the
number of blocks to track increases?)

Mind you, this is a *perfect* solution we're talking about being impossible
within memory contraints. Sorting on inodes is just a heuristic, and there are
others to choose from.
