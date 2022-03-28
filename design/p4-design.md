# CSCC69 PROJECT 4: FILE SYSTEMS DESIGN DOCUMENT

### GROUP

Logan Chester <logan.chester@mail.utoronto.ca>

Jonathan Yao <jonathan.yao@mail.utoronto.ca>

### PRELIMINARIES

Tsung-Han Sher, S. CSCI 350: Pintos Guide. 

We tried so hard to implement everything but it just wasn't feasible with the knowledge we have from class and the limited resources we are given when trying to solve the project. Our inode/buffer implementations are by no means perfect but they at least got some of the tests to pass. We barely touched on subdirectories due to time constraints and a lack of understanding, and as such, our design doc is incomplete. It would be greatly appreciated if the TA marking this was slightly lenient in light of the current learning environment and the difficulty of PintOS.

## INDEXED AND EXTENSIBLE FILES

### DATA STRUCTURES

**A1:** Copy here the declaration of each new or changed `struct` or
`struct` member, global or static variable, `typedef`, or
enumeration.  Identify the purpose of each in 25 words or less.

```
#define INODE_INDIRECT_BLOCKS_PER_SECTOR BLOCK_SECTOR_SIZE / 4

#define INODE_DIRECT_BLOCKS 98
#define INODE_DIRECT_INDEX 0
#define INODE_INDIRECT_BLOCKS 1
#define INODE_INDIRECT_INDEX INODE_DIRECT_INDEX + INODE_DIRECT_BLOCKS
#define INODE_DOUBLY_INDIRECT_BLOCKS 1
#define INODE_DOUBLY_INDIRECT_INDEX                                           \
  INODE_INDIRECT_INDEX + INODE_DOUBLY_INDIRECT_BLOCKS

#define SECTORS_USED                                                          \
  INODE_DIRECT_BLOCKS + INODE_INDIRECT_BLOCKS + INODE_DOUBLY_INDIRECT_BLOCKS
#define SECTORS_UNUSED 24

static char zeros[BLOCK_SECTOR_SIZE];
```

```
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  /** Data sectors */
  block_sector_t
      blocks[SECTORS_USED]; /* Direct, indirect, and doubly indirect blocks. */
  block_sector_t parent;    /* Sector where the parent directory resides. */

  bool directory; /* True if this inode is a directory. */

  off_t length;                    /* File size in bytes. */
  unsigned magic;                  /* Magic number. */
  uint32_t unused[SECTORS_UNUSED]; /* Unused sectors to ensure that inode_disk
                                      is exactly BLOCK_SECTOR_SIZE. */
};
```

```
/* In-memory inode. */
struct inode
{
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct inode_disk data; /* Inode content. */

  struct lock lock; /* Protects inode content. */
};
```

```
/* Tracks what type of indirect block is being dealt with. */
static enum indirect_state { BASE, SINGLE, DOUBLE };
```

**A2:** What is the maximum size of a file supported by your inode
structure?  Show your work.

Our inode structure supports a maximum file size of 8.11 mb. This is because each inode has 98 direct blocks, 1 indirect block, and 1 doubly indirect block. Since the block sector size is 512 bytes and each indirect block contains 128 points, we have the following calculation: ((98 * 512) + (1 * 128 * 512) + (1 * 128 * 128 * 512)) / (1024 * 1024) = 8.11 MB

### SYNCHRONIZATION

**A3:** Explain how your code avoids a race if two processes attempt to
extend a file at the same time.

We use the lock corresponding to an inode to lock the byte_to_sector function to ensure that only one sector is allocated for a request for file extension. We also lock the inode's length in order to ensure that the largest file size persists if multiple processes are attempting to extend a file. 

**A4:** Suppose processes A and B both have file F open, both
positioned at end-of-file.  If A reads and B writes F at the same
time, A may read all, part, or none of what B writes.  However, A
may not read data other than what B writes, e.g. if B writes
nonzero data, A is not allowed to see all zeros.  Explain how your
code avoids this race.

Updating the length of a file (the length field in inode_disk, or inode->data) only occurs after file extension has occured, so A would only read what B writes if it attempts to read after B has finished writing. If it tries to read while B is writing, the length will remain unchanged and A will not see any new data that B may be writing.

**A5:** Explain how your synchronization design provides "fairness".
File access is "fair" if readers cannot indefinitely block writers
or vice versa.  That is, many processes reading from a file cannot
prevent forever another process from writing the file, and many
processes writing to a file cannot prevent another process forever
from reading the file.

Readers and writers are not treated differently when trying to acquire an inode lock. We decided to avoid the use of condition variables for simplicity. By not treating readers and writers differently, the synchronization deesign provides fairness.

### RATIONALE

A6: Is your inode structure a multilevel index?  If so, why did you
choose this particular combination of direct, indirect, and doubly
indirect blocks?  If not, why did you choose an alternative inode
structure, and what advantages and disadvantages does your
structure have, compared to a multilevel index?

Our inode structure is a multilevel index. We chose to construct each inode with 98 direct blocks, 1 indirect block, and 1 doubly indirect block. We chose this approach after reading the supplementary guide which suggested 10 direct blocks, 1 indirect block, and 1 doubly indirect block. While this works for our implementation, it does not provide enough sectors to pass many of the filesys tests. We arbitrarily chose 98 direct blocks in order to make the number of total blocks a clean 100, but choosing the maximum allowable number of blocks (122 in our case) while ensuring inode_disk is exactly of size BLOCK_SECTOR_SIZE also works perfectly fine with our implementation.

## SUBDIRECTORIES

### DATA STRUCTURES

**B1:** Copy here the declaration of each new or changed `struct` or
`struct` member, global or static variable, `typedef`, or
enumeration.  Identify the purpose of each in 25 words or less.

```
struct dir
{
...
struct lock lock; /* added directory lock to avoid race conditions when modifying a directory*/
}
```

### ALGORITHMS

**B2:** Describe your code for traversing a user-specified path.  How
do traversals of absolute and relative paths differ?

We ran out of time to implement a working subdirectory implementation, but the way we were parsing a path is by first finding the starting point of the path. if the path given starts with a "/", the path starts from the root directory. If it doesn't start with a "/", the path is a relative path, starting at the current working directory. With the starting point found, we would iterate through the string, and using dir_lookup to check every directory seperated by "/" in the path string, while keeping track of the current directory we are in during the traversal. When the last directory in the path string is found, we return that directory.

### SYNCHRONIZATION

**B4:** How do you prevent races on directory entries?  For example,
only one of two simultaneous attempts to remove a single file
should succeed, as should only one of two simultaneous attempts to
create a file with the same name, and so on.

When reading and writing to a directory, we used a lock to ensure synchronization is preserved. When there is two simultaneous attempts to remove a file, one process will execute dir_remove first, acquiring the lock so that the second process will wait until the first process releases the lock. The second process won't be able to find the file it is trying to remove and return the appropriate response. In the case where two processes try to create a file with the same name simultaneously, the same lock acquiring and release process occurs, and when the second process runs, it will find that the name already exists and it won't add the file. Modifying a directory always requires the directory lock to avoid race conditions.

**B5:** Does your implementation allow a directory to be removed if it
is open by a process or if it is in use as a process's current
working directory?  If so, what happens to that process's future
file system operations?  If not, how do you prevent it?

### RATIONALE

**B6:** Explain why you chose to represent the current directory of a
process the way you did.

We tried to implement the directories by having the directory of the process be represented in the thread struct by the directory's inode sector number. This way when the process changes directories, it would make the process of closing the previous directory inode easier to implement as we wouldn't have to search for the directory's inode sector number using its name, path, or some other identifier.

## BUFFER CACHE

### DATA STRUCTURES

**C1:** Copy here the declaration of each new or changed `struct` or
`struct` member, global or static variable, `typedef`, or
enumeration.  Identify the purpose of each in 25 words or less.

```
/* Cache entry for our buffer. Contains data that helps determine eviction
 * candidates. */
struct buffer_cache_entry
{
  char data[BLOCK_SECTOR_SIZE]; /* data for cache */

  bool dirty;         /* dirty bit */
  bool valid;         /* valid bit, false on init, always true after */
  bool used_recently; /* for clock algorithm */

  block_sector_t disk_sector; /* sector the cache represents */

  struct lock lock; /* lock for cache members */
};
```

```
/* Index for the clock algorithm used for cache eviction. */
static size_t clock = 0;

/* Our buffer cache, as suggested in the supplemental docs. */
static struct buffer_cache_entry buffer_cache[BUFFER_CACHE_SIZE];

/* A lock to maintain synchronization on the buffer cache. */
static struct lock buffer_cache_lock;
```

### ALGORITHMS

**C2:** Describe how your cache replacement algorithm chooses a cache
block to evict.

Our cache replacement algorithm is based on the clock algorithm. The algorithm first checks if a cache block is empty (invalid). If it is, it immediately returns this cache block and no proper eviction is needed. If it is not empty (valid), then the algorithm checks whether it was used recently. If it was, it is not picked for eviction. It is, however, marked as no longer recently used. If the cache block was not used recently, the algorithm picks this cache block to return. If it is dirty, it waits to acquire the lock for this particular cache block, it flushes the existing data from the cache block to the disk, and then returns it. This is the eviction process. If no cache block is picked for eviction, a second iteration of the algorithm occurs. A target cache block is ensured on the second iteration because each cache block that was passed in the previous iteration was marked as not recently used.

**C3:** Describe your implementation of write-behind.

We only partially implemented write-behind as we ran out of time. We use a dirty bit to ensure that dirty entries are written to disk whenever they are evicted. In order to further implement write-behind, we should have created a separate process that is started immediately after the buffer cache is initialized. This process would wake every so often and write all dirty entries to the the disk in a non-blocking fashion. We didnt get to this portion of the assignment until it was too late :(

**C4:** Describe your implementation of read-ahead.

We were unable to implement read-ahead for the same reasons as above. It likely would have looked similar to write-behind, as in a process that is started right after the buffer-cache is initialized. It is mentioned that it needs to be asynchronus, so our implementation would have ensureed this. Condition variables may have been useful for this part, but I am not sure about the rest f the implementation because, again, we ran out of time :(

### SYNCHRONIZATION

**C5:** When one process is actively reading or writing data in a
buffer cache block, how are other processes prevented from evicting
that block?

Every buffer cache block has a lock associated with it that a process must acquire before trying to evict a block. This way, no block is evicted while one process is actively reading or writing data in this particular cache block.

**C6:** During the eviction of a block from the cache, how are other
processes prevented from attempting to access the block?

Same rationale as above, every cache block has a lock associated that must be held by the process attempting to access the block.

### RATIONALE

**C7:** Describe a file workload likely to benefit from buffer caching,
and workloads likely to benefit from read-ahead and write-behind.

A file workload likely to benefit from buffer caching are ones that are repeatedly reading and writing the same blocks.

We did not fully implement read-ahead and write-behind, however based on our understanding of the concept, file workloads that benefit from read-ahead would likely be accessing files in sequential order. On the other hand, file workloads that may benefit from write-behind would access many files without often modifying them. 

## SURVEY QUESTIONS

Answering these questions is optional, but it will help us improve the
course in future quarters.  Feel free to tell us anything you
want--these questions are just to spur your thoughts.  You may also
choose to respond anonymously in the course evaluations at the end of
the quarter.

In your opinion, was this assignment, or any one of the three problems
in it, too easy or too hard?  Did it take too long or too little time?

Logan: This class and all the assignments were an absolute nightmare. I have already made my issues well known in previous surveys. I have never been so unhappy and unhealthy as I have been while taking this class. All I think about is how to solve PintOS problems, it's like I never have a moment off school. It is way too challenging and we are given very little to help us even start attempting to solve the projects. Please never use this structure for this course again for the sake of future students.

Did you find that working on a particular part of the assignment gave
you greater insight into some aspect of OS design?

Is there some particular fact or hint we should give students in
future quarters to help them solve the problems?  Conversely, did you
find any of our guidance to be misleading?

Do you have any suggestions for the TAs to more effectively assist
students in future quarters?

Any other comments?
