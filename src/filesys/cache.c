#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include <list.h>
#include <string.h>

/* Index for the clock algorithm used for cache eviction. */
static size_t clock = 0;

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

/* Our buffer cache, as suggested in the supplemental docs. */
static struct buffer_cache_entry buffer_cache[BUFFER_CACHE_SIZE];

/* A lock to maintain synchronization on the buffer cache. */
static struct lock buffer_cache_lock;

/* Initialize the buffer cache system. */
void
buffer_cache_init (void)
{
  lock_init (&buffer_cache_lock);

  struct buffer_cache_entry *bce;

  for (size_t i = 0; i < BUFFER_CACHE_SIZE; i++)
    {
      bce = &buffer_cache[i];
      bce->valid = false;
      bce->dirty = false;
      lock_init (&bce->lock);
    }
}

/* write data from the entry to the block */
static void
buffer_cache_flush_entry (struct buffer_cache_entry *bce)
{
  block_write (fs_device, bce->disk_sector, bce->data);
  bce->dirty = false;
}

/* Destroy the buffer cache system. */
void
buffer_cache_close (void)
{
  lock_acquire (&buffer_cache_lock);

  struct buffer_cache_entry *bce;

  for (size_t i = 0; i < BUFFER_CACHE_SIZE; i++)
    {
      bce = &buffer_cache[i];
      if (bce->valid && bce->dirty)
        {
          lock_acquire (&bce->lock);
          buffer_cache_flush_entry (bce);
          lock_release (&bce->lock);
        }
    }

  lock_release (&buffer_cache_lock);
}

/* Obtian a buffer cache entry from the buffer cache if it exists. */
static struct buffer_cache_entry *
buffer_cache_lookup (block_sector_t sector)
{
  struct buffer_cache_entry *bce;

  for (size_t i = 0; i < BUFFER_CACHE_SIZE; i++)
    {
      bce = &buffer_cache[i];
      if (bce->valid && bce->disk_sector == sector)
        return bce;
    }

  return NULL;
}

/* evict an entry using the clock algorithm */
static struct buffer_cache_entry *
buffer_cache_evict (void)
{
  struct buffer_cache_entry *bce;

  // clock algorithm
  while (true)
    {
      bce = &buffer_cache[clock];

      /* if it is invalid (empty), just return it */
      if (!bce->valid)
        return bce;

      /* don't return it if it is used recently */
      if (bce->used_recently)
        bce->used_recently = false;
      else
        {
          /* if dirty, write to block */
          if (bce->dirty)
            {
              lock_acquire (&bce->lock);
              buffer_cache_flush_entry (bce);
              lock_release (&bce->lock);
            }

          /* now it is safe to return */
          bce->valid = false;
          return bce;
        }

      clock++;
      clock %= BUFFER_CACHE_SIZE;
    }
}

/* read to buffer cache from block */
void
buffer_cache_read (block_sector_t sector, void *buffer)
{
  lock_acquire (&buffer_cache_lock);

  struct buffer_cache_entry *bce = buffer_cache_lookup (sector);

  /* if entry is not in the cache */
  if (bce == NULL)
    {
      bce = buffer_cache_evict ();

      bce->disk_sector = sector;
      bce->valid = true;
      bce->dirty = false;

      /* read data from block */
      lock_acquire (&bce->lock);
      block_read (fs_device, sector, bce->data);
      lock_release (&bce->lock);
    }

  bce->used_recently = true;

  /* copy from cache data into memory */
  memcpy (buffer, bce->data, BLOCK_SECTOR_SIZE);

  lock_release (&buffer_cache_lock);
}

/* write from block to buffer cache */
void
buffer_cache_write (block_sector_t sector, void *buffer)
{
  lock_acquire (&buffer_cache_lock);

  struct buffer_cache_entry *bce = buffer_cache_lookup (sector);

  /* if entry is not in the cache */
  if (bce == NULL)
    {
      bce = buffer_cache_evict ();

      bce->disk_sector = sector;
      bce->valid = true;
    }

  bce->dirty = true;
  bce->used_recently = true;

  /* copy data from memory to cache */
  memcpy (bce->data, buffer, BLOCK_SECTOR_SIZE);

  lock_release (&buffer_cache_lock);
}