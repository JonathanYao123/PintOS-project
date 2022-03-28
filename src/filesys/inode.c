#include "filesys/inode.h"
#include "devices/block.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/syscall.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <string.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

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
#define SECTORS_UNUSED                                                        \
  24 /* 125 - SECTORS_USED, for some reason subtraction isnt working for this \
      */

static char
    zeros[BLOCK_SECTOR_SIZE]; /* Used throughout the program, especially when
                                 writing to the buffer. */

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  block_sector_t
      blocks[SECTORS_USED]; /* Direct, indirect, and doubly indirect blocks. */
  block_sector_t parent;    /* Sector where the parent directory resides. */

  bool directory; /* True if this inode is a directory. */

  off_t length;                    /* File size in bytes. */
  unsigned magic;                  /* Magic number. */
  uint32_t unused[SECTORS_UNUSED]; /* Unused sectors to ensure that inode_disk
                                      is exactly BLOCK_SECTOR_SIZE. */
};

static bool inode_alloc (struct inode_disk *disk_inode);
static bool inode_extend (struct inode_disk *disk_inode, size_t length);
static void inode_free (struct inode *inode);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

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

/* Used to lock this inode on occasion. Helps with byte_to_sector and updating
 * length of disk_inode, but causes more tests to fail when used for updating
 * other disk_inode fields. */
static bool
inode_lock (struct inode *inode)
{
  bool lock_held = lock_held_by_current_thread (&inode->lock);
  if (!lock_held)
    lock_acquire (&inode->lock);
  return lock_held;
}

/* Used to unlock this inode on occasion. Helps with byte_to_sector and
 * updating length of disk_inode, but causes more tests to fail when used for
 * updating other disk_inode fields. */
static void
inode_unlock (struct inode *inode, bool held)
{
  if (!held)
    lock_release (&inode->lock);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);

  block_sector_t result;

  bool lock_held = inode_lock (inode);

  if (pos < inode->data.length)
    {
      /* sector index */
      off_t index = pos / BLOCK_SECTOR_SIZE;

      /* direct blocks*/
      if (index < INODE_DIRECT_BLOCKS)
        {
          result = inode->data.blocks[index];
          goto finish;
        }

      /* an indirect block */
      index -= INODE_DIRECT_BLOCKS;
      if (index < INODE_INDIRECT_BLOCKS_PER_SECTOR)
        {
          block_sector_t indirect_blocks[INODE_INDIRECT_BLOCKS_PER_SECTOR];

          buffer_cache_read (inode->data.blocks[INODE_INDIRECT_INDEX],
                             indirect_blocks);

          result = indirect_blocks[index];
          goto finish;
        }

      /* a doubly indirect block */
      index -= INODE_INDIRECT_BLOCKS_PER_SECTOR;
      if (index < INODE_INDIRECT_BLOCKS_PER_SECTOR
                      * INODE_INDIRECT_BLOCKS_PER_SECTOR)
        {
          block_sector_t
              doubly_indirect_blocks[INODE_INDIRECT_BLOCKS_PER_SECTOR];

          buffer_cache_read (inode->data.blocks[INODE_DOUBLY_INDIRECT_INDEX],
                             doubly_indirect_blocks);
          buffer_cache_read (
              doubly_indirect_blocks[index / INODE_INDIRECT_BLOCKS_PER_SECTOR],
              doubly_indirect_blocks);
          result = doubly_indirect_blocks[index
                                          % INODE_INDIRECT_BLOCKS_PER_SECTOR];
          goto finish;
        }

      /* something went wrong */
      result = EXIT_FAILURE;
    }
  else
    result = EXIT_FAILURE;

finish:
  inode_unlock (inode, lock_held);
  return result;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool directory)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->directory = directory;
      disk_inode->parent = ROOT_DIR_SECTOR;

      success = inode_alloc (disk_inode);
      if (success)
        buffer_cache_write (sector, disk_inode);
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->lock);

  buffer_cache_read (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  lock_acquire (&inode->lock);
  if (inode != NULL)
    inode->open_cnt++;
  lock_release (&inode->lock);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;
  bool lock_held = inode_lock (inode);
  inode->open_cnt--;
  inode_unlock (inode, lock_held);

  /* Release resources if this was the last opener. */
  if (inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          inode_free (inode);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached.

   Only change here is that block_read becomes buffer_cache_read.
   */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          buffer_cache_read (sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          buffer_cache_read (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode).

   Only change here is that block_read becomes buffer_cache_write.
   */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* if beyond the EOF, extend the file */
  if (byte_to_sector (inode, offset + size - 1) == -1u)
    {
      /* extend the file offset + size bytes */
      if (!inode_extend (&inode->data, offset + size))
        /* unable to extend the file */
        return 0;

      /* write back the new file size */
      bool lock_held = inode_lock (inode);
      inode->data.length = offset + size > inode->data.length
                               ? offset + size
                               : inode->data.length;
      inode_unlock (inode, lock_held);

      buffer_cache_write (inode->sector, &inode->data);
    }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          buffer_cache_write (sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            buffer_cache_read (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          buffer_cache_write (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  bool lock_held = inode_lock (inode);
  inode->deny_write_cnt++;
  inode_unlock (inode, lock_held);

  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);

  lock_acquire (&inode->lock);
  inode->deny_write_cnt--;
  lock_release (&inode->lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* Returns whether the file is a directory or not. */
bool
inode_is_directory (const struct inode *inode)
{
  return inode->data.directory;
}

/* Returns whether the file is removed or not. */
bool
inode_is_removed (const struct inode *inode)
{
  return inode->removed;
}

/* Allocate space for the inode based on length stored in the structure. */
static bool
inode_alloc (struct inode_disk *disk_inode)
{
  return inode_extend (disk_inode, disk_inode->length);
}

/* Extend the block provided by allocating one sector in the free map. */
static bool
inode_extend_block (block_sector_t *block)
{
  if (!free_map_allocate (1, block))
    return false;
  buffer_cache_write (*block, zeros);
  return true;
}

/* Extend the direct block by REMAINING_SECTORS. */
static bool
inode_extend_direct (struct inode_disk *id, size_t remaining_sectors)
{
  for (size_t i = 0; i < remaining_sectors; i++)
    {
      /* make sure the block isn't already allocated */
      if (id->blocks[i] == 0)
        {
          if (!inode_extend_block (&id->blocks[i]))
            return false;
        }
    }
  return true;
}

/* Tracks what type of indirect block is being dealt with. */
static enum indirect_state { BASE, SINGLE, DOUBLE };

/* Extend an indirect block by remaining_sectors recursively. */
static bool
inode_extend_indirect (block_sector_t *sector, size_t remaining_sectors,
                       enum indirect_state state)
{
  block_sector_t indirect_blocks[INODE_INDIRECT_BLOCKS_PER_SECTOR];
  size_t i, sectors_to_extend = remaining_sectors;

  /* true unless extension of 0 sector fails */
  bool base_result = true;

  if (*sector == 0)
    base_result = inode_extend_block (sector);

  switch (state)
    {
    case BASE: /* base case, return base result */
      return base_result;
    case SINGLE: /* singly indirect, initiate recursion */
      buffer_cache_read (*sector, &indirect_blocks);
      for (i = 0; i < sectors_to_extend; ++i)
        {
          if (!inode_extend_indirect (&indirect_blocks[i], 1, BASE))
            return false;
          remaining_sectors--;
        }
      goto finish;
    case DOUBLE: /* doubly indirect, initiate recursion */
      buffer_cache_read (*sector, &indirect_blocks);
      for (i = 0; i < sectors_to_extend; ++i)
        {
          if (!inode_extend_indirect (&indirect_blocks[i],
                                      INODE_INDIRECT_BLOCKS_PER_SECTOR,
                                      SINGLE))
            return false;
          remaining_sectors -= INODE_INDIRECT_BLOCKS_PER_SECTOR;
        }
      goto finish;
    }

finish:
  buffer_cache_write (*sector, &indirect_blocks);
  return true;
}

/* Extend inode by length bytes. */
static bool
inode_extend (struct inode_disk *disk_inode, size_t length)
{
  if (length < 0)
    return false;

  size_t remaining_sectors = bytes_to_sectors (length);
  size_t i, sectors_to_extend;

  /* direct blocks */
  sectors_to_extend = remaining_sectors < INODE_DIRECT_BLOCKS
                          ? remaining_sectors
                          : INODE_DIRECT_BLOCKS;
  if (!inode_extend_direct (disk_inode, remaining_sectors))
    /* unsuccessful extension */
    return false;
  remaining_sectors -= sectors_to_extend;

  if (remaining_sectors == 0)
    return true;

  /* indirect block */
  sectors_to_extend = remaining_sectors < INODE_INDIRECT_BLOCKS_PER_SECTOR
                          ? remaining_sectors
                          : INODE_INDIRECT_BLOCKS_PER_SECTOR;
  if (!inode_extend_indirect (&disk_inode->blocks[INODE_INDIRECT_INDEX],
                              sectors_to_extend, SINGLE))
    /* unsuccessful extension */
    return false;
  remaining_sectors -= sectors_to_extend;

  if (remaining_sectors == 0)
    return true;

  /* doubly indirect block */
  sectors_to_extend
      = remaining_sectors < INODE_INDIRECT_BLOCKS_PER_SECTOR
                                * INODE_INDIRECT_BLOCKS_PER_SECTOR
            ? remaining_sectors
            : INODE_INDIRECT_BLOCKS_PER_SECTOR
                  * INODE_INDIRECT_BLOCKS_PER_SECTOR;
  if (!inode_extend_indirect (&disk_inode->blocks[INODE_DOUBLY_INDIRECT_INDEX],
                              sectors_to_extend, DOUBLE))
    /* unsuccessful extension */
    return false;
  remaining_sectors -= sectors_to_extend;

  if (remaining_sectors == 0)
    return true;

  return false;
}

/* Deallocate the provided indirect block sector recursively. */
static size_t
inode_free_indirect (block_sector_t sector, size_t remaining_sectors,
                     enum indirect_state state)
{
  block_sector_t indirect_blocks[INODE_INDIRECT_BLOCKS_PER_SECTOR];
  size_t i, sectors_freed;

  switch (state)
    {
    case BASE: /* base case, just free the sector */
      free_map_release (sector, 1);
      sectors_freed += 1;
      return sectors_freed;
    case SINGLE: /* singly indirect block, initiate recursion */
      buffer_cache_read (sector, &indirect_blocks);
      while (i < INODE_INDIRECT_BLOCKS_PER_SECTOR && remaining_sectors > 0)
        {
          sectors_freed += inode_free_indirect (indirect_blocks[i],
                                                remaining_sectors, BASE);
          remaining_sectors -= sectors_freed;
          i++;
        }
    case DOUBLE: /* doubly indirect block, initiate recursion */
      buffer_cache_read (sector, &indirect_blocks);
      while (i < INODE_INDIRECT_BLOCKS_PER_SECTOR && remaining_sectors > 0)
        {
          sectors_freed += inode_free_indirect (indirect_blocks[i],
                                                remaining_sectors, SINGLE);
          remaining_sectors -= sectors_freed;
          i++;
        }
    }

  free_map_release (sector, 1);
  return sectors_freed;
}

/* Deallocate a direct block sector. */
static size_t
inode_free_direct (struct inode_disk *id, size_t remaining_sectors)
{
  size_t i = 0;
  while (i < INODE_DIRECT_BLOCKS && remaining_sectors > 0)
    {
      free_map_release (id->blocks[i], 1);
      remaining_sectors--;
      i++;
    }
  return i;
}

/* Deallocate the provided INODE. */
static void
inode_free (struct inode *inode)
{
  size_t remaining_sectors = bytes_to_sectors (inode->data.length);

  if (remaining_sectors)
    {
      size_t i, sectors_to_free;

      /* direct blocks */
      remaining_sectors -= inode_free_direct (&inode->data, remaining_sectors);

      /* indirect block */
      if (remaining_sectors)
        remaining_sectors
            -= inode_free_indirect (inode->data.blocks[INODE_INDIRECT_INDEX],
                                    remaining_sectors, SINGLE);

      /* doubly indirect block */
      if (remaining_sectors)
        remaining_sectors -= inode_free_indirect (
            inode->data.blocks[INODE_DOUBLY_INDIRECT_INDEX], remaining_sectors,
            DOUBLE);
    }
}
