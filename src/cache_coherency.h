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

#ifndef CACHE_COHERENCY_H
#define CACHE_COHERENCY_H

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "cache_meta.h"
#include "cache_block.h"

/**
 * Revalidate cached metadata against backend.
 * Compares mtime and size to determine if cache entry is still valid.
 * 
 * @param path File path
 * @param cached_entry Cached metadata entry
 * @param backend_stat Backend stat structure
 * @return true if cache is valid, false if stale
 */
bool cache_coherency_validate_meta(const char *path,
                                    const cache_meta_entry_t *cached_entry,
                                    const struct stat *backend_stat);

/**
 * Revalidate directory cache against backend.
 * Compares directory mtime to determine if directory listing is still valid.
 * 
 * @param path Directory path
 * @param cached_mtime Cached directory mtime
 * @param backend_stat Backend stat structure
 * @return true if cache is valid, false if stale
 */
bool cache_coherency_validate_dir(const char *path,
                                   time_t cached_mtime,
                                   const struct stat *backend_stat);

/**
 * Check if file has been modified on backend and invalidate if needed.
 * Called on open() to ensure cache coherency.
 * 
 * @param meta_ctx Metadata cache context
 * @param block_ctx Block cache context
 * @param path File path
 * @param backend_stat Backend stat structure
 * @return 0 on success, -1 on error
 */
int cache_coherency_check_and_invalidate(cache_meta_ctx_t *meta_ctx,
                                          cache_block_ctx_t *block_ctx,
                                          const char *path,
                                          const struct stat *backend_stat);

#endif /* CACHE_COHERENCY_H */
