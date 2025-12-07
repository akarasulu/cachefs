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

#ifndef CACHE_META_H
#define CACHE_META_H

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdbool.h>

/* Cache entry types */
typedef enum {
    CACHE_ENTRY_FILE = 1,
    CACHE_ENTRY_DIR = 2,
    CACHE_ENTRY_NEG = 3  /* Negative entry (file not found) */
} cache_entry_type_t;

/* Metadata cache entry structure */
typedef struct {
    cache_entry_type_t type;
    off_t size;
    time_t mtime;
    time_t ctime;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    ino_t ino;  /* Cached inode number */
    time_t cached_at;
    time_t valid_until;
} cache_meta_entry_t;

/* Directory entry structure */
typedef struct {
    char *name;
    cache_entry_type_t type;
} cache_dir_entry_t;

/* Opaque cache metadata handle */
typedef struct cache_meta_ctx cache_meta_ctx_t;

/**
 * Initialize metadata cache.
 * @param cache_root Root directory for cache storage
 * @param meta_ttl Metadata TTL in seconds
 * @param dir_ttl Directory listing TTL in seconds
 * @param debug Enable debug logging
 * @return Cache context or NULL on error
 */
cache_meta_ctx_t *cache_meta_init(const char *cache_root, 
                                   int meta_ttl, 
                                   int dir_ttl,
                                   bool debug);

/**
 * Lookup metadata entry in cache.
 * @param ctx Cache context
 * @param path File path
 * @param entry Output metadata entry
 * @param valid Output validity flag (true if not expired)
 * @return 0 on success (cache hit), -1 on miss or error
 */
int cache_meta_lookup(cache_meta_ctx_t *ctx, 
                      const char *path,
                      cache_meta_entry_t *entry,
                      bool *valid);

/**
 * Store metadata entry in cache.
 * @param ctx Cache context
 * @param path File path
 * @param stbuf Stat buffer from backend
 * @return 0 on success, -1 on error
 */
int cache_meta_store(cache_meta_ctx_t *ctx,
                     const char *path,
                     const struct stat *stbuf);

/**
 * Store negative entry (file not found).
 * @param ctx Cache context
 * @param path File path
 * @return 0 on success, -1 on error
 */
int cache_meta_store_negative(cache_meta_ctx_t *ctx, const char *path);

/**
 * Invalidate metadata entry.
 * @param ctx Cache context
 * @param path File path
 * @return 0 on success, -1 on error
 */
int cache_meta_invalidate(cache_meta_ctx_t *ctx, const char *path);

/**
 * Lookup directory listing in cache.
 * @param ctx Cache context
 * @param path Directory path
 * @param entries Output array of directory entries (caller must free)
 * @param count Output count of entries
 * @param dir_mtime Output directory mtime
 * @param valid Output validity flag
 * @return 0 on success (cache hit), -1 on miss or error
 */
int cache_dir_lookup(cache_meta_ctx_t *ctx,
                     const char *path,
                     cache_dir_entry_t **entries,
                     size_t *count,
                     time_t *dir_mtime,
                     bool *valid);

/**
 * Store directory listing in cache.
 * @param ctx Cache context
 * @param path Directory path
 * @param entries Array of directory entries
 * @param count Number of entries
 * @param dir_mtime Directory mtime
 * @return 0 on success, -1 on error
 */
int cache_dir_store(cache_meta_ctx_t *ctx,
                    const char *path,
                    const cache_dir_entry_t *entries,
                    size_t count,
                    time_t dir_mtime);

/**
 * Invalidate directory listing.
 * @param ctx Cache context
 * @param path Directory path
 * @return 0 on success, -1 on error
 */
int cache_dir_invalidate(cache_meta_ctx_t *ctx, const char *path);

/**
 * Free directory entry array.
 * @param entries Array of directory entries
 * @param count Number of entries
 */
void cache_dir_entries_free(cache_dir_entry_t *entries, size_t count);

/**
 * Shutdown metadata cache.
 * @param ctx Cache context
 */
void cache_meta_destroy(cache_meta_ctx_t *ctx);

#endif /* CACHE_META_H */
