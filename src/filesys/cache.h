#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "filesys/off_t.h"

#define BUFFER_CACHE_SIZE 64

void buffer_cache_init (void);
void buffer_cache_close (void);

void buffer_cache_read (block_sector_t sector, void *buffer);
void buffer_cache_write (block_sector_t sector, void *buffer);

#endif