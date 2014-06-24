# libdj

An implementation of [disk geometry-aware file system access](http://www.ecsl.cs.sunysb.edu/tr/TR240.pdf) for ext2 and later.

## Inode sorting

You may ask yourself, "Why sort on the inode numbers at all, if what we really 
care about is the order of the data blocks?" My friend, you ask a reasonable 
question. Consider the following.

1. We can only keep a certain number of blocks in memory at a time.
2. We can only keep a certain number of files in memory at a time. This is 
mostly due to clients that maintain state on a per-file basis, which is an 
entirely reasonable thing to do. If we feed them too many files at once, they 
blow out all their memory.
3. Therefore, we have to choose which files to buffer in memory at any one 
time. We can do this:
  1. intelligently, by iterating through the files' metadata and figuring out 
  which sets of files have the most-contiguous blocks, or
  2. stupidly, by sorting the files on their inode numbers and working our way 
  through them, on the assumption that the blocks of close-together inodes will 
  be close together themselves.

I have chosen option (2) of point (3), for the moment. You may ask yourself, 
"Doesn't that merely punt on the problem we're trying to solve? This whole 
exercise about sorting blocks is predicated on the file system not allocating 
blocks perfectly efficiently, but now we're going to assume that the ordering 
of inodes is reasonably close to the ordering of their blocks?"

Yes. Because I'm too lazy at the moment to do any better (yay, open source), 
and because a perfect implementation of option (i) doesn't exist in the general 
case, *assuming* that such an implementation requires non-constant (and 
non-trivial) memory asymptotically. In other words, if we have a really low 
constraint on available memory and a really large file system to examine, there 
ain't no way to pick sets of files with mostly-contiguous blocks without eating 
up all your memory tracking where those blocks are. At least, I think this is 
the case. (Edit: Maybe you could do it fuzzily, and dial up the fuzziness as 
the number of blocks to track increases?)

Mind you, this is a *perfect* solution we're talking about being impossible 
within memory contraints. Sorting on inodes is just a heuristic, and there are 
others to choose from.
