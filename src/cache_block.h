/*
    Copyright 2024 Alex Karasulu <akarasulu@gmail.com>

    This file is part of CacheFS.

    CacheFS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    CacheFS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CacheFS.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef CACHE_BLOCK_H
#define CACHE_BLOCK_H

#include <sys/types.h>
#include <stdbool.h>

#define DEFAULT_BLOCK_SIZE (256 * 1024)  /* 256 KiB */

/* Opaque block cache handle */
typedef struct cache_block_ctx cache_block_ctx_t;

/**
 * Initialize block cache.
 * @param cache_root Root directory for cache storage
 * @param block_size Block size in bytes
 * @param max_cache_size Maximum total cache size in bytes (0 = unlimited)
 * @param debug Enable debug logging
 * @return Cache context or NULL on error
 */
cache_block_ctx_t *cache_block_init(const char *cache_root,
                                     size_t block_size,
                                     size_t max_cache_size,
                                     bool debug);

/**
 * Check if a block exists in cache.
 * @param ctx Cache context
 * @param path File path
 * @param block_idx Block index
 * @return true if block exists, false otherwise
 */
bool cache_block_exists(cache_block_ctx_t *ctx,
                        const char *path,
                        size_t block_idx);

/**
 * Read a block from cache.
 * @param ctx Cache context
 * @param path File path
 * @param block_idx Block index
 * @param buf Output buffer
 * @param size Number of bytes to read
 * @param offset Offset within block
 * @return Number of bytes read, or -1 on error
 */
ssize_t cache_block_read(cache_block_ctx_t *ctx,
                         const char *path,
                         size_t block_idx,
                         char *buf,
                         size_t size,
                         size_t offset);

/**
 * Write a block to cache.
 * @param ctx Cache context
 * @param path File path
 * @param block_idx Block index
 * @param buf Input buffer
 * @param size Number of bytes to write
 * @return 0 on success, -1 on error
 */
int cache_block_write(cache_block_ctx_t *ctx,
                      const char *path,
                      size_t block_idx,
                      const char *buf,
                      size_t size);

/**
 * Invalidate a range of blocks.
 * @param ctx Cache context
 * @param path File path
 * @param offset File offset where write occurred
 * @param size Number of bytes written
 * @return 0 on success, -1 on error
 */
int cache_block_invalidate_range(cache_block_ctx_t *ctx,
                                  const char *path,
                                  off_t offset,
                                  size_t size);

/**
 * Invalidate all blocks for a file.
 * @param ctx Cache context
 * @param path File path
 * @return 0 on success, -1 on error
 */
int cache_block_invalidate_file(cache_block_ctx_t *ctx, const char *path);

/**
 * Get current cache statistics.
 * @param ctx Cache context
 * @param current_size_out Current cache size in bytes (can be NULL)
 * @param max_size_out Maximum cache size in bytes (can be NULL)
 */
void cache_block_get_stats(cache_block_ctx_t *ctx,
                           size_t *current_size_out,
                           size_t *max_size_out);

/**
 * Shutdown block cache.
 * @param ctx Cache context
 */
void cache_block_destroy(cache_block_ctx_t *ctx);

#endif /* CACHE_BLOCK_H */
