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

#include "cache_coherency.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>

bool cache_coherency_validate_meta(const char *path,
                                    const cache_meta_entry_t *cached_entry,
                                    const struct stat *backend_stat)
{
    if (cached_entry == NULL || backend_stat == NULL) {
        return false;
    }

    /* Compare mtime and size */
    bool valid = (cached_entry->mtime == backend_stat->st_mtime &&
                  cached_entry->size == backend_stat->st_size);

    DPRINTF("cache_coherency_validate_meta: %s %s (mtime: %ld vs %ld, size: %ld vs %ld)",
            path, valid ? "valid" : "stale",
            cached_entry->mtime, backend_stat->st_mtime,
            cached_entry->size, backend_stat->st_size);

    return valid;
}

bool cache_coherency_validate_dir(const char *path,
                                   time_t cached_mtime,
                                   const struct stat *backend_stat)
{
    if (backend_stat == NULL) {
        return false;
    }

    /* Compare directory mtime */
    bool valid = (cached_mtime == backend_stat->st_mtime);

    DPRINTF("cache_coherency_validate_dir: %s %s (mtime: %ld vs %ld)",
            path, valid ? "valid" : "stale",
            cached_mtime, backend_stat->st_mtime);

    return valid;
}

int cache_coherency_check_and_invalidate(cache_meta_ctx_t *meta_ctx,
                                          cache_block_ctx_t *block_ctx,
                                          const char *path,
                                          const struct stat *backend_stat)
{
    if (meta_ctx == NULL || path == NULL || backend_stat == NULL) {
        return -1;
    }

    /* Lookup cached metadata */
    cache_meta_entry_t cached;
    bool valid;
    
    if (cache_meta_lookup(meta_ctx, path, &cached, &valid) == 0) {
        /* Cache entry exists, check if it's stale */
        if (!cache_coherency_validate_meta(path, &cached, backend_stat)) {
            DPRINTF("cache_coherency_check_and_invalidate: invalidating stale cache for %s", path);
            
            /* Invalidate metadata */
            cache_meta_invalidate(meta_ctx, path);
            
            /* Invalidate blocks if block cache available */
            if (block_ctx != NULL) {
                cache_block_invalidate_file(block_ctx, path);
            }
        }
    }

    return 0;
}
