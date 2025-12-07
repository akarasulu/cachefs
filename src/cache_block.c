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

#include "cache_block.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <dirent.h>
#include <dirent.h>

/* Block cache context */
struct cache_block_ctx {
    char *blocks_dir;
    size_t block_size;
    size_t max_cache_size;
    size_t current_cache_size;  /* Current total cache size */
    bool debug;
};

/* Hash function for path (simple DJB2) */
static unsigned long hash_path(const char *path)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *path++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

/* Get block file path: blocks/XX/YY/hash-blockidx */
static char *get_block_path(cache_block_ctx_t *ctx, const char *path, size_t block_idx)
{
    unsigned long hash = hash_path(path);
    char *block_path = malloc(PATH_MAX);
    if (block_path == NULL) {
        return NULL;
    }

    unsigned char h1 = (hash >> 8) & 0xFF;
    unsigned char h2 = hash & 0xFF;
    
    snprintf(block_path, PATH_MAX, "%s/%02x/%02x/%016lx-%zu",
             ctx->blocks_dir, h1, h2, hash, block_idx);
    
    return block_path;
}

/* Create directory hierarchy for block */
static int create_block_dir(const char *block_path)
{
    char *dir_path = strdup(block_path);
    if (dir_path == NULL) {
        return -1;
    }

    /* Find last '/' */
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash == NULL) {
        free(dir_path);
        return -1;
    }
    *last_slash = '\0';

    /* Create parent directories */
    char *p = dir_path;
    while (*p == '/') p++;
    
    while ((p = strchr(p, '/')) != NULL) {
        *p = '\0';
        mkdir(dir_path, 0700);
        *p = '/';
        p++;
    }
    mkdir(dir_path, 0700);

    free(dir_path);
    return 0;
}

/* Structure for tracking blocks during LRU scan */
struct block_info {
    char *path;
    time_t atime;
    off_t size;
};

/* Comparison function for qsort (oldest first) */
static int compare_block_atime(const void *a, const void *b)
{
    const struct block_info *ba = a;
    const struct block_info *bb = b;
    if (ba->atime < bb->atime) return -1;
    if (ba->atime > bb->atime) return 1;
    return 0;
}

/* Calculate current cache size by scanning all blocks */
static size_t calculate_cache_size(cache_block_ctx_t *ctx)
{
    size_t total = 0;
    char path_l1[PATH_MAX], path_l2[PATH_MAX];
    
    DIR *dp1 = opendir(ctx->blocks_dir);
    if (dp1 == NULL) {
        return 0;
    }
    
    struct dirent *de1;
    while ((de1 = readdir(dp1)) != NULL) {
        if (de1->d_name[0] == '.') continue;
        
        snprintf(path_l1, PATH_MAX, "%s/%s", ctx->blocks_dir, de1->d_name);
        DIR *dp2 = opendir(path_l1);
        if (dp2 == NULL) continue;
        
        struct dirent *de2;
        while ((de2 = readdir(dp2)) != NULL) {
            if (de2->d_name[0] == '.') continue;
            
            snprintf(path_l2, PATH_MAX, "%s/%s", path_l1, de2->d_name);
            DIR *dp3 = opendir(path_l2);
            if (dp3 == NULL) continue;
            
            struct dirent *de3;
            while ((de3 = readdir(dp3)) != NULL) {
                if (de3->d_name[0] == '.') continue;
                
                char block_path[PATH_MAX];
                snprintf(block_path, PATH_MAX, "%s/%s", path_l2, de3->d_name);
                
                struct stat st;
                if (stat(block_path, &st) == 0) {
                    total += st.st_size;
                }
            }
            closedir(dp3);
        }
        closedir(dp2);
    }
    closedir(dp1);
    
    return total;
}

/* Evict blocks until cache size is below threshold */
static int evict_lru_blocks(cache_block_ctx_t *ctx, size_t target_size)
{
    if (ctx->current_cache_size <= target_size) {
        return 0;
    }
    
    /* Collect all blocks with their access times */
    struct block_info *blocks = NULL;
    size_t block_count = 0;
    size_t block_capacity = 1024;
    
    blocks = malloc(block_capacity * sizeof(struct block_info));
    if (blocks == NULL) {
        return -1;
    }
    
    char path_l1[PATH_MAX], path_l2[PATH_MAX];
    DIR *dp1 = opendir(ctx->blocks_dir);
    if (dp1 == NULL) {
        free(blocks);
        return -1;
    }
    
    struct dirent *de1;
    while ((de1 = readdir(dp1)) != NULL) {
        if (de1->d_name[0] == '.') continue;
        
        snprintf(path_l1, PATH_MAX, "%s/%s", ctx->blocks_dir, de1->d_name);
        DIR *dp2 = opendir(path_l1);
        if (dp2 == NULL) continue;
        
        struct dirent *de2;
        while ((de2 = readdir(dp2)) != NULL) {
            if (de2->d_name[0] == '.') continue;
            
            snprintf(path_l2, PATH_MAX, "%s/%s", path_l1, de2->d_name);
            DIR *dp3 = opendir(path_l2);
            if (dp3 == NULL) continue;
            
            struct dirent *de3;
            while ((de3 = readdir(dp3)) != NULL) {
                if (de3->d_name[0] == '.') continue;
                
                char block_path[PATH_MAX];
                snprintf(block_path, PATH_MAX, "%s/%s", path_l2, de3->d_name);
                
                struct stat st;
                if (stat(block_path, &st) == 0) {
                    /* Grow array if needed */
                    if (block_count >= block_capacity) {
                        block_capacity *= 2;
                        struct block_info *new_blocks = realloc(blocks, 
                            block_capacity * sizeof(struct block_info));
                        if (new_blocks == NULL) {
                            for (size_t i = 0; i < block_count; i++) {
                                free(blocks[i].path);
                            }
                            free(blocks);
                            closedir(dp3);
                            closedir(dp2);
                            closedir(dp1);
                            return -1;
                        }
                        blocks = new_blocks;
                    }
                    
                    blocks[block_count].path = strdup(block_path);
                    blocks[block_count].atime = st.st_atime;
                    blocks[block_count].size = st.st_size;
                    block_count++;
                }
            }
            closedir(dp3);
        }
        closedir(dp2);
    }
    closedir(dp1);
    
    if (block_count == 0) {
        free(blocks);
        return 0;
    }
    
    /* Sort by access time (oldest first) */
    qsort(blocks, block_count, sizeof(struct block_info), compare_block_atime);
    
    /* Evict oldest blocks until below target */
    size_t evicted_size = 0;
    size_t evicted_count = 0;
    
    for (size_t i = 0; i < block_count && ctx->current_cache_size - evicted_size > target_size; i++) {
        if (unlink(blocks[i].path) == 0) {
            evicted_size += blocks[i].size;
            evicted_count++;
        }
    }
    
    ctx->current_cache_size -= evicted_size;
    
    if (ctx->debug && evicted_count > 0) {
        DPRINTF("evict_lru_blocks: evicted %zu blocks (%zu bytes), cache now %zu bytes",
                evicted_count, evicted_size, ctx->current_cache_size);
    }
    
    /* Free block info array */
    for (size_t i = 0; i < block_count; i++) {
        free(blocks[i].path);
    }
    free(blocks);
    
    return 0;
}

cache_block_ctx_t *cache_block_init(const char *cache_root,
                                     size_t block_size,
                                     size_t max_cache_size,
                                     bool debug)
{
    if (cache_root == NULL) {
        return NULL;
    }

    cache_block_ctx_t *ctx = calloc(1, sizeof(cache_block_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    ctx->block_size = block_size > 0 ? block_size : DEFAULT_BLOCK_SIZE;
    ctx->max_cache_size = max_cache_size;
    ctx->debug = debug;

    /* Create blocks directory path */
    ctx->blocks_dir = malloc(PATH_MAX);
    if (ctx->blocks_dir == NULL) {
        free(ctx);
        return NULL;
    }
    snprintf(ctx->blocks_dir, PATH_MAX, "%s/blocks", cache_root);

    /* Create blocks directory */
    mkdir(ctx->blocks_dir, 0700);

    /* Calculate current cache size */
    ctx->current_cache_size = calculate_cache_size(ctx);

    if (debug) {
        DPRINTF("cache_block_init: initialized at %s (block_size=%zu, max_size=%zu, current=%zu)",
                ctx->blocks_dir, ctx->block_size, ctx->max_cache_size, ctx->current_cache_size);
    }

    return ctx;
}

bool cache_block_exists(cache_block_ctx_t *ctx,
                        const char *path,
                        size_t block_idx)
{
    if (ctx == NULL || path == NULL) {
        return false;
    }

    char *block_path = get_block_path(ctx, path, block_idx);
    if (block_path == NULL) {
        return false;
    }

    struct stat st;
    bool exists = (stat(block_path, &st) == 0);
    
    free(block_path);
    return exists;
}

ssize_t cache_block_read(cache_block_ctx_t *ctx,
                         const char *path,
                         size_t block_idx,
                         char *buf,
                         size_t size,
                         size_t offset)
{
    if (ctx == NULL || path == NULL || buf == NULL) {
        return -1;
    }

    char *block_path = get_block_path(ctx, path, block_idx);
    if (block_path == NULL) {
        return -1;
    }

    int fd = open(block_path, O_RDONLY);
    free(block_path);
    
    if (fd == -1) {
        return -1;
    }

    ssize_t bytes = pread(fd, buf, size, offset);
    close(fd);

    if (ctx->debug && bytes > 0) {
        DPRINTF("cache_block_read: read %zd bytes from %s block %zu",
                bytes, path, block_idx);
    }

    return bytes;
}

int cache_block_write(cache_block_ctx_t *ctx,
                      const char *path,
                      size_t block_idx,
                      const char *buf,
                      size_t size)
{
    if (ctx == NULL || path == NULL || buf == NULL) {
        return -1;
    }

    char *block_path = get_block_path(ctx, path, block_idx);
    if (block_path == NULL) {
        return -1;
    }

    /* Create directory hierarchy */
    if (create_block_dir(block_path) != 0) {
        free(block_path);
        return -1;
    }

    int fd = open(block_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) {
        free(block_path);
        return -1;
    }

    ssize_t written = write(fd, buf, size);
    close(fd);

    if (written != (ssize_t)size) {
        free(block_path);
        return -1;
    }

    /* Update cache size and trigger eviction if needed */
    ctx->current_cache_size += written;
    
    if (ctx->max_cache_size > 0 && ctx->current_cache_size > ctx->max_cache_size) {
        /* Evict until we're at 90% of max */
        size_t target = (ctx->max_cache_size * 9) / 10;
        evict_lru_blocks(ctx, target);
    }

    free(block_path);

    if (ctx->debug) {
        DPRINTF("cache_block_write: wrote %zu bytes to %s block %zu (cache: %zu/%zu)",
                size, path, block_idx, ctx->current_cache_size, ctx->max_cache_size);
    }

    return 0;
}

int cache_block_invalidate_range(cache_block_ctx_t *ctx,
                                  const char *path,
                                  off_t offset,
                                  size_t size)
{
    if (ctx == NULL || path == NULL) {
        return -1;
    }

    size_t start_block = offset / ctx->block_size;
    size_t end_block = (offset + size) / ctx->block_size;

    for (size_t i = start_block; i <= end_block; i++) {
        char *block_path = get_block_path(ctx, path, i);
        if (block_path != NULL) {
            struct stat st;
            if (stat(block_path, &st) == 0) {
                if (unlink(block_path) == 0) {
                    ctx->current_cache_size -= st.st_size;
                }
            }
            free(block_path);
        }
    }

    if (ctx->debug) {
        DPRINTF("cache_block_invalidate_range: invalidated blocks %zu-%zu for %s",
                start_block, end_block, path);
    }

    return 0;
}

int cache_block_invalidate_file(cache_block_ctx_t *ctx, const char *path)
{
    if (ctx == NULL || path == NULL) {
        return -1;
    }

    /* Invalidate all blocks by removing entire hash directory */
    unsigned long hash = hash_path(path);
    char dir_path[PATH_MAX];
    unsigned char h1 = (hash >> 8) & 0xFF;
    unsigned char h2 = hash & 0xFF;
    
    snprintf(dir_path, PATH_MAX, "%s/%02x/%02x", ctx->blocks_dir, h1, h2);

    /* Remove all blocks matching this hash prefix */
    DIR *dp = opendir(dir_path);
    if (dp != NULL) {
        struct dirent *de;
        char block_prefix[32];
        snprintf(block_prefix, sizeof(block_prefix), "%016lx-", hash);
        
        while ((de = readdir(dp)) != NULL) {
            if (strncmp(de->d_name, block_prefix, strlen(block_prefix)) == 0) {
                char full_path[PATH_MAX];
                snprintf(full_path, PATH_MAX, "%s/%s", dir_path, de->d_name);
                
                struct stat st;
                if (stat(full_path, &st) == 0) {
                    if (unlink(full_path) == 0) {
                        ctx->current_cache_size -= st.st_size;
                    }
                }
            }
        }
        closedir(dp);
    }

    if (ctx->debug) {
        DPRINTF("cache_block_invalidate_file: invalidated all blocks for %s", path);
    }

    return 0;
}

void cache_block_get_stats(cache_block_ctx_t *ctx,
                           size_t *current_size_out,
                           size_t *max_size_out)
{
    if (ctx == NULL) {
        return;
    }
    
    if (current_size_out != NULL) {
        *current_size_out = ctx->current_cache_size;
    }
    
    if (max_size_out != NULL) {
        *max_size_out = ctx->max_cache_size;
    }
}

void cache_block_destroy(cache_block_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    free(ctx->blocks_dir);
    free(ctx);

    DPRINTF("cache_block_destroy: block cache destroyed");
}
