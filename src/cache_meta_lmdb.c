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

#include "cache_meta.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sqlite3.h>
#include <unistd.h>
#include <limits.h>

#define META_DB_NAME "metadata.db"

/* Cache metadata context */
struct cache_meta_ctx {
    sqlite3 *db;
    sqlite3_stmt *insert_meta_stmt;
    sqlite3_stmt *select_meta_stmt;
    sqlite3_stmt *delete_meta_stmt;
    char *cache_root;
    int meta_ttl;
    int dir_ttl;
    bool debug;
};

/* Serialized metadata entry for LMDB storage */
typedef struct {
    uint8_t type;
    int64_t size;
    int64_t mtime;
    int64_t ctime;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    int64_t cached_at;
    int64_t valid_until;
} __attribute__((packed)) meta_entry_serialized_t;

cache_meta_ctx_t *cache_meta_init(const char *cache_root,
                                   int meta_ttl,
                                   int dir_ttl,
                                   bool debug)
{
    if (cache_root == NULL) {
        DPRINTF("cache_meta_init: cache_root is NULL");
        return NULL;
    }

    cache_meta_ctx_t *ctx = calloc(1, sizeof(cache_meta_ctx_t));
    if (ctx == NULL) {
        DPRINTF("cache_meta_init: failed to allocate context");
        return NULL;
    }

    ctx->cache_root = strdup(cache_root);
    ctx->meta_ttl = meta_ttl;
    ctx->dir_ttl = dir_ttl;
    ctx->debug = debug;

    /* Create cache directory if it doesn't exist */
    mkdir(cache_root, 0700);
    
    /* Put LMDB in /tmp to ensure it's completely outside any mounted filesystem
     * This avoids any potential conflicts with FUSE operations */
    char lmdb_path[PATH_MAX];
    unsigned long cache_hash = 5381;
    for (const char *p = cache_root; *p; p++) {
        cache_hash = ((cache_hash << 5) + cache_hash) + *p;
    }
    snprintf(lmdb_path, PATH_MAX, "/tmp/cachefs-lmdb-%08lx", cache_hash & 0xFFFFFFFF);
    mkdir(lmdb_path, 0700);

    /* Initialize LMDB environment in /tmp */
    int rc = mdb_env_create(&ctx->env);
    if (rc != 0) {
        DPRINTF("cache_meta_init: mdb_env_create failed: %s", mdb_strerror(rc));
        free(ctx->cache_root);
        free(ctx);
        return NULL;
    }

    mdb_env_set_maxdbs(ctx->env, 2);  /* metadata + directories */
    mdb_env_set_mapsize(ctx->env, META_DB_SIZE);

    /* Open LMDB with flags optimized for FUSE:
     * MDB_NOSYNC - Don't force sync on commit (faster, risk on crash)
     * MDB_WRITEMAP - Use writable memory map (better for multi-threaded)
     * MDB_MAPASYNC - Async flush for MDB_WRITEMAP
     */
    rc = mdb_env_open(ctx->env, lmdb_path, MDB_NOSYNC | MDB_WRITEMAP | MDB_MAPASYNC, 0600);
    if (rc != 0) {
        DPRINTF("cache_meta_init: mdb_env_open failed: %s", mdb_strerror(rc));
        mdb_env_close(ctx->env);
        free(ctx->cache_root);
        free(ctx);
        return NULL;
    }

    /* Open metadata database */
    MDB_txn *txn;
    rc = mdb_txn_begin(ctx->env, NULL, 0, &txn);
    if (rc != 0) {
        DPRINTF("cache_meta_init: mdb_txn_begin failed: %s", mdb_strerror(rc));
        mdb_env_close(ctx->env);
        free(ctx->cache_root);
        free(ctx);
        return NULL;
    }

    rc = mdb_dbi_open(txn, META_DB_NAME, MDB_CREATE, &ctx->meta_dbi);
    if (rc != 0) {
        DPRINTF("cache_meta_init: mdb_dbi_open(meta) failed: %s", mdb_strerror(rc));
        mdb_txn_abort(txn);
        mdb_env_close(ctx->env);
        free(ctx->cache_root);
        free(ctx);
        return NULL;
    }

    rc = mdb_dbi_open(txn, DIR_DB_NAME, MDB_CREATE, &ctx->dir_dbi);
    if (rc != 0) {
        DPRINTF("cache_meta_init: mdb_dbi_open(dir) failed: %s", mdb_strerror(rc));
        mdb_txn_abort(txn);
        mdb_env_close(ctx->env);
        free(ctx->cache_root);
        free(ctx);
        return NULL;
    }

    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        DPRINTF("cache_meta_init: mdb_txn_commit failed: %s", mdb_strerror(rc));
        mdb_env_close(ctx->env);
        free(ctx->cache_root);
        free(ctx);
        return NULL;
    }

    if (debug) {
        DPRINTF("cache_meta_init: initialized cache at %s (meta_ttl=%d, dir_ttl=%d)",
                cache_root, meta_ttl, dir_ttl);
    }

    return ctx;
}

int cache_meta_lookup(cache_meta_ctx_t *ctx,
                      const char *path,
                      cache_meta_entry_t *entry,
                      bool *valid)
{
    if (ctx == NULL || path == NULL || entry == NULL) {
        return -1;
    }

    MDB_txn *txn;
    int rc = mdb_txn_begin(ctx->env, NULL, MDB_RDONLY, &txn);
    if (rc != 0) {
        DPRINTF("cache_meta_lookup: mdb_txn_begin failed: %s", mdb_strerror(rc));
        return -1;
    }

    MDB_val key = { .mv_size = strlen(path), .mv_data = (void*)path };
    MDB_val data;

    rc = mdb_get(txn, ctx->meta_dbi, &key, &data);
    if (rc == MDB_NOTFOUND) {
        mdb_txn_abort(txn);
        if (ctx->debug) {
            DPRINTF("cache_meta_lookup: cache miss for %s", path);
        }
        return -1;
    } else if (rc != 0) {
        DPRINTF("cache_meta_lookup: mdb_get failed: %s", mdb_strerror(rc));
        mdb_txn_abort(txn);
        return -1;
    }

    if (data.mv_size != sizeof(meta_entry_serialized_t)) {
        DPRINTF("cache_meta_lookup: corrupt entry size for %s", path);
        mdb_txn_abort(txn);
        return -1;
    }

    meta_entry_serialized_t *stored = (meta_entry_serialized_t*)data.mv_data;
    entry->type = stored->type;
    entry->size = stored->size;
    entry->mtime = stored->mtime;
    entry->ctime = stored->ctime;
    entry->mode = stored->mode;
    entry->uid = stored->uid;
    entry->gid = stored->gid;
    entry->cached_at = stored->cached_at;
    entry->valid_until = stored->valid_until;

    mdb_txn_abort(txn);

    /* Check validity */
    time_t now = time(NULL);
    if (valid != NULL) {
        *valid = (now < entry->valid_until);
    }

    if (ctx->debug) {
        DPRINTF("cache_meta_lookup: cache hit for %s (valid=%d)", 
                path, valid ? *valid : -1);
    }

    return 0;
}

int cache_meta_store(cache_meta_ctx_t *ctx,
                     const char *path,
                     const struct stat *stbuf)
{
    if (ctx == NULL || path == NULL || stbuf == NULL) {
        return -1;
    }

    time_t now = time(NULL);
    meta_entry_serialized_t entry = {
        .type = S_ISDIR(stbuf->st_mode) ? CACHE_ENTRY_DIR : CACHE_ENTRY_FILE,
        .size = stbuf->st_size,
        .mtime = stbuf->st_mtime,
        .ctime = stbuf->st_ctime,
        .mode = stbuf->st_mode,
        .uid = stbuf->st_uid,
        .gid = stbuf->st_gid,
        .cached_at = now,
        .valid_until = now + ctx->meta_ttl
    };

    MDB_txn *txn;
    int rc = mdb_txn_begin(ctx->env, NULL, 0, &txn);
    if (rc != 0) {
        DPRINTF("cache_meta_store: mdb_txn_begin failed: %s", mdb_strerror(rc));
        return -1;
    }

    MDB_val key = { .mv_size = strlen(path), .mv_data = (void*)path };
    MDB_val data = { .mv_size = sizeof(entry), .mv_data = &entry };

    rc = mdb_put(txn, ctx->meta_dbi, &key, &data, 0);
    if (rc != 0) {
        DPRINTF("cache_meta_store: mdb_put failed: %s", mdb_strerror(rc));
        mdb_txn_abort(txn);
        return -1;
    }

    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        DPRINTF("cache_meta_store: mdb_txn_commit failed: %s", mdb_strerror(rc));
        return -1;
    }

    if (ctx->debug) {
        DPRINTF("cache_meta_store: stored entry for %s", path);
    }

    return 0;
}

int cache_meta_store_negative(cache_meta_ctx_t *ctx, const char *path)
{
    if (ctx == NULL || path == NULL) {
        return -1;
    }

    time_t now = time(NULL);
    meta_entry_serialized_t entry = {
        .type = CACHE_ENTRY_NEG,
        .cached_at = now,
        .valid_until = now + 2  /* Short TTL for negative entries */
    };

    MDB_txn *txn;
    int rc = mdb_txn_begin(ctx->env, NULL, 0, &txn);
    if (rc != 0) {
        return -1;
    }

    MDB_val key = { .mv_size = strlen(path), .mv_data = (void*)path };
    MDB_val data = { .mv_size = sizeof(entry), .mv_data = &entry };

    rc = mdb_put(txn, ctx->meta_dbi, &key, &data, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return -1;
    }

    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        return -1;
    }

    if (ctx->debug) {
        DPRINTF("cache_meta_store_negative: stored negative entry for %s", path);
    }

    return 0;
}

int cache_meta_invalidate(cache_meta_ctx_t *ctx, const char *path)
{
    if (ctx == NULL || path == NULL) {
        return -1;
    }

    MDB_txn *txn;
    int rc = mdb_txn_begin(ctx->env, NULL, 0, &txn);
    if (rc != 0) {
        return -1;
    }

    MDB_val key = { .mv_size = strlen(path), .mv_data = (void*)path };

    rc = mdb_del(txn, ctx->meta_dbi, &key, NULL);
    if (rc == MDB_NOTFOUND) {
        mdb_txn_abort(txn);
        return 0;  /* Already invalidated */
    } else if (rc != 0) {
        mdb_txn_abort(txn);
        return -1;
    }

    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        return -1;
    }

    if (ctx->debug) {
        DPRINTF("cache_meta_invalidate: invalidated entry for %s", path);
    }

    return 0;
}

int cache_dir_lookup(cache_meta_ctx_t *ctx,
                     const char *path,
                     cache_dir_entry_t **entries,
                     size_t *count,
                     time_t *dir_mtime,
                     bool *valid)
{
    /* TODO: Implement directory cache lookup */
    (void)ctx;
    (void)path;
    (void)entries;
    (void)count;
    (void)dir_mtime;
    (void)valid;
    return -1;  /* Not implemented yet */
}

int cache_dir_store(cache_meta_ctx_t *ctx,
                    const char *path,
                    const cache_dir_entry_t *entries,
                    size_t count,
                    time_t dir_mtime)
{
    /* TODO: Implement directory cache store */
    (void)ctx;
    (void)path;
    (void)entries;
    (void)count;
    (void)dir_mtime;
    return -1;  /* Not implemented yet */
}

int cache_dir_invalidate(cache_meta_ctx_t *ctx, const char *path)
{
    /* TODO: Implement directory cache invalidation */
    (void)ctx;
    (void)path;
    return -1;  /* Not implemented yet */
}

void cache_dir_entries_free(cache_dir_entry_t *entries, size_t count)
{
    if (entries == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        free(entries[i].name);
    }
    free(entries);
}

void cache_meta_destroy(cache_meta_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->env != NULL) {
        mdb_env_close(ctx->env);
    }

    free(ctx->cache_root);
    free(ctx);

    DPRINTF("cache_meta_destroy: cache destroyed");
}
