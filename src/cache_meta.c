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
    sqlite3_stmt *insert_dir_stmt;
    sqlite3_stmt *select_dir_stmt;
    sqlite3_stmt *delete_dir_stmt;
    char *cache_root;
    int meta_ttl;
    int dir_ttl;
    bool debug;
};

cache_meta_ctx_t *cache_meta_init(const char *cache_root,
                                   int meta_ttl,
                                   int dir_ttl,
                                   bool debug)
{
    fprintf(stderr, "[cache_meta_init] START\n");
    
    if (cache_root == NULL) {
        fprintf(stderr, "[cache_meta_init] ERROR: cache_root is NULL\n");
        return NULL;
    }

    fprintf(stderr, "[cache_meta_init] cache_root=%s\n", cache_root);

    cache_meta_ctx_t *ctx = calloc(1, sizeof(cache_meta_ctx_t));
    if (ctx == NULL) {
        fprintf(stderr, "[cache_meta_init] ERROR: calloc failed\n");
        return NULL;
    }

    ctx->cache_root = strdup(cache_root);
    ctx->meta_ttl = meta_ttl;
    ctx->dir_ttl = dir_ttl;
    ctx->debug = debug;

    /* Create cache directory if it doesn't exist */
    fprintf(stderr, "[cache_meta_init] Creating cache directory...\n");
    int mkdir_result = mkdir(cache_root, 0700);
    fprintf(stderr, "[cache_meta_init] mkdir result=%d, errno=%d (%s)\n", mkdir_result, errno, strerror(errno));
    
    /* Open SQLite database */
    char db_path[PATH_MAX];
    snprintf(db_path, PATH_MAX, "%s/%s", cache_root, META_DB_NAME);
    fprintf(stderr, "[cache_meta_init] Opening SQLite at: %s\n", db_path);
    
    int rc = sqlite3_open(db_path, &ctx->db);
    fprintf(stderr, "[cache_meta_init] sqlite3_open result=%d\n", rc);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[cache_meta_init] ERROR: sqlite3_open failed: %s\n", sqlite3_errmsg(ctx->db));
        DPRINTF("cache_meta_init: sqlite3_open failed: %s", sqlite3_errmsg(ctx->db));
        free(ctx->cache_root);
        free(ctx);
        return NULL;
    }
    fprintf(stderr, "[cache_meta_init] SQLite opened successfully\n");

    /* Enable WAL mode for better concurrency, don't wait on locks */
    sqlite3_busy_timeout(ctx->db, 100);  /* 100ms timeout */
    sqlite3_exec(ctx->db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_exec(ctx->db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
    sqlite3_exec(ctx->db, "PRAGMA temp_store=MEMORY", NULL, NULL, NULL);

    /* Create metadata table */
    const char *create_sql = 
        "CREATE TABLE IF NOT EXISTS metadata ("
        "  path TEXT PRIMARY KEY,"
        "  type INTEGER,"
        "  size INTEGER,"
        "  mtime INTEGER,"
        "  ctime INTEGER,"
        "  mode INTEGER,"
        "  uid INTEGER,"
        "  gid INTEGER,"
        "  ino INTEGER,"
        "  cached_at INTEGER,"
        "  valid_until INTEGER"
        ")";
    
    char *errmsg = NULL;
    rc = sqlite3_exec(ctx->db, create_sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        DPRINTF("cache_meta_init: create table failed: %s", errmsg);
        sqlite3_free(errmsg);
        sqlite3_close(ctx->db);
        free(ctx->cache_root);
        free(ctx);
        return NULL;
    }

    /* Create directory entries table */
    const char *create_dir_sql =
        "CREATE TABLE IF NOT EXISTS dir_entries ("
        "  dir_path TEXT,"
        "  entry_name TEXT,"
        "  entry_type INTEGER,"
        "  dir_mtime INTEGER,"
        "  cached_at INTEGER,"
        "  valid_until INTEGER,"
        "  PRIMARY KEY (dir_path, entry_name)"
        ")";
    
    rc = sqlite3_exec(ctx->db, create_dir_sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        DPRINTF("cache_meta_init: create dir_entries table failed: %s", errmsg);
        sqlite3_free(errmsg);
        sqlite3_close(ctx->db);
        free(ctx->cache_root);
        free(ctx);
        return NULL;
    }

    /* Prepare statements */
    const char *insert_sql = 
        "INSERT OR REPLACE INTO metadata VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_prepare_v2(ctx->db, insert_sql, -1, &ctx->insert_meta_stmt, NULL);

    const char *select_sql = "SELECT * FROM metadata WHERE path = ?";
    sqlite3_prepare_v2(ctx->db, select_sql, -1, &ctx->select_meta_stmt, NULL);

    const char *delete_sql = "DELETE FROM metadata WHERE path = ?";
    sqlite3_prepare_v2(ctx->db, delete_sql, -1, &ctx->delete_meta_stmt, NULL);

    /* Directory cache statements */
    const char *insert_dir_sql =
        "INSERT OR REPLACE INTO dir_entries VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_prepare_v2(ctx->db, insert_dir_sql, -1, &ctx->insert_dir_stmt, NULL);

    const char *select_dir_sql =
        "SELECT entry_name, entry_type, dir_mtime, cached_at, valid_until "
        "FROM dir_entries WHERE dir_path = ? ORDER BY entry_name";
    sqlite3_prepare_v2(ctx->db, select_dir_sql, -1, &ctx->select_dir_stmt, NULL);

    const char *delete_dir_sql = "DELETE FROM dir_entries WHERE dir_path = ?";
    sqlite3_prepare_v2(ctx->db, delete_dir_sql, -1, &ctx->delete_dir_stmt, NULL);

    if (debug) {
        DPRINTF("cache_meta_init: initialized at %s (meta_ttl=%d, dir_ttl=%d)",
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

    sqlite3_reset(ctx->select_meta_stmt);
    sqlite3_bind_text(ctx->select_meta_stmt, 1, path, -1, SQLITE_STATIC);

    int rc = sqlite3_step(ctx->select_meta_stmt);
    if (rc != SQLITE_ROW) {
        return -1;  /* Not found */
    }

    entry->type = sqlite3_column_int(ctx->select_meta_stmt, 1);
    entry->size = sqlite3_column_int64(ctx->select_meta_stmt, 2);
    entry->mtime = sqlite3_column_int64(ctx->select_meta_stmt, 3);
    entry->ctime = sqlite3_column_int64(ctx->select_meta_stmt, 4);
    entry->mode = sqlite3_column_int(ctx->select_meta_stmt, 5);
    entry->uid = sqlite3_column_int(ctx->select_meta_stmt, 6);
    entry->gid = sqlite3_column_int(ctx->select_meta_stmt, 7);
    entry->ino = sqlite3_column_int64(ctx->select_meta_stmt, 8);
    entry->cached_at = sqlite3_column_int64(ctx->select_meta_stmt, 9);
    entry->valid_until = sqlite3_column_int64(ctx->select_meta_stmt, 10);

    if (valid != NULL) {
        time_t now = time(NULL);
        *valid = (now < entry->valid_until);
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
    
    sqlite3_reset(ctx->insert_meta_stmt);
    sqlite3_bind_text(ctx->insert_meta_stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_int(ctx->insert_meta_stmt, 2, CACHE_ENTRY_FILE);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 3, stbuf->st_size);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 4, stbuf->st_mtime);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 5, stbuf->st_ctime);
    sqlite3_bind_int(ctx->insert_meta_stmt, 6, stbuf->st_mode);
    sqlite3_bind_int(ctx->insert_meta_stmt, 7, stbuf->st_uid);
    sqlite3_bind_int(ctx->insert_meta_stmt, 8, stbuf->st_gid);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 9, stbuf->st_ino);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 10, now);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 11, now + ctx->meta_ttl);

    int rc = sqlite3_step(ctx->insert_meta_stmt);
    if (rc != SQLITE_DONE) {
        DPRINTF("cache_meta_store: insert failed: %s", sqlite3_errmsg(ctx->db));
        return -1;
    }

    return 0;
}

int cache_meta_store_negative(cache_meta_ctx_t *ctx, const char *path)
{
    if (ctx == NULL || path == NULL) {
        return -1;
    }

    time_t now = time(NULL);
    
    sqlite3_reset(ctx->insert_meta_stmt);
    sqlite3_bind_text(ctx->insert_meta_stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_int(ctx->insert_meta_stmt, 2, CACHE_ENTRY_NEG);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 3, 0);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 4, 0);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 5, 0);
    sqlite3_bind_int(ctx->insert_meta_stmt, 6, 0);
    sqlite3_bind_int(ctx->insert_meta_stmt, 7, 0);
    sqlite3_bind_int(ctx->insert_meta_stmt, 8, 0);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 9, 0);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 10, now);
    sqlite3_bind_int64(ctx->insert_meta_stmt, 11, now + ctx->meta_ttl);

    int rc = sqlite3_step(ctx->insert_meta_stmt);
    if (rc != SQLITE_DONE) {
        return -1;
    }

    return 0;
}

int cache_meta_invalidate(cache_meta_ctx_t *ctx, const char *path)
{
    if (ctx == NULL || path == NULL) {
        return -1;
    }

    sqlite3_reset(ctx->delete_meta_stmt);
    sqlite3_bind_text(ctx->delete_meta_stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_step(ctx->delete_meta_stmt);

    return 0;
}

/* Directory cache functions */
int cache_dir_lookup(cache_meta_ctx_t *ctx,
                     const char *path,
                     cache_dir_entry_t **entries,
                     size_t *count,
                     time_t *dir_mtime,
                     bool *valid)
{
    if (ctx == NULL || path == NULL || entries == NULL || count == NULL) {
        return -1;
    }

    sqlite3_reset(ctx->select_dir_stmt);
    sqlite3_bind_text(ctx->select_dir_stmt, 1, path, -1, SQLITE_STATIC);

    /* Count entries first */
    size_t entry_count = 0;
    while (sqlite3_step(ctx->select_dir_stmt) == SQLITE_ROW) {
        entry_count++;
    }

    if (entry_count == 0) {
        return -1;  /* No cached entries */
    }

    /* Allocate array */
    *entries = calloc(entry_count, sizeof(cache_dir_entry_t));
    if (*entries == NULL) {
        return -1;
    }

    /* Reset and read entries */
    sqlite3_reset(ctx->select_dir_stmt);
    sqlite3_bind_text(ctx->select_dir_stmt, 1, path, -1, SQLITE_STATIC);

    size_t i = 0;
    time_t first_valid_until = 0;
    time_t first_mtime = 0;
    while (sqlite3_step(ctx->select_dir_stmt) == SQLITE_ROW && i < entry_count) {
        const char *name = (const char *)sqlite3_column_text(ctx->select_dir_stmt, 0);
        (*entries)[i].name = strdup(name);
        (*entries)[i].type = sqlite3_column_int(ctx->select_dir_stmt, 1);
        
        if (i == 0) {
            first_mtime = sqlite3_column_int64(ctx->select_dir_stmt, 2);
            first_valid_until = sqlite3_column_int64(ctx->select_dir_stmt, 4);
        }
        i++;
    }

    *count = entry_count;
    if (dir_mtime != NULL) {
        *dir_mtime = first_mtime;
    }
    
    if (valid != NULL) {
        time_t now = time(NULL);
        *valid = (now < first_valid_until);
    }

    return 0;
}

int cache_dir_store(cache_meta_ctx_t *ctx, const char *path,
                    const cache_dir_entry_t *entries, size_t count,
                    time_t dir_mtime)
{
    if (ctx == NULL || path == NULL || entries == NULL) {
        return -1;
    }

    /* Begin transaction for bulk inserts */
    sqlite3_exec(ctx->db, "BEGIN TRANSACTION", NULL, NULL, NULL);

    /* Delete old entries for this directory */
    sqlite3_reset(ctx->delete_dir_stmt);
    sqlite3_bind_text(ctx->delete_dir_stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_step(ctx->delete_dir_stmt);

    /* Insert new entries */
    time_t now = time(NULL);
    time_t valid_until = now + ctx->dir_ttl;

    for (size_t i = 0; i < count; i++) {
        sqlite3_reset(ctx->insert_dir_stmt);
        sqlite3_bind_text(ctx->insert_dir_stmt, 1, path, -1, SQLITE_STATIC);
        sqlite3_bind_text(ctx->insert_dir_stmt, 2, entries[i].name, -1, SQLITE_STATIC);
        sqlite3_bind_int(ctx->insert_dir_stmt, 3, entries[i].type);
        sqlite3_bind_int64(ctx->insert_dir_stmt, 4, dir_mtime);
        sqlite3_bind_int64(ctx->insert_dir_stmt, 5, now);
        sqlite3_bind_int64(ctx->insert_dir_stmt, 6, valid_until);

        int rc = sqlite3_step(ctx->insert_dir_stmt);
        if (rc != SQLITE_DONE) {
            DPRINTF("cache_dir_store: insert failed: %s", sqlite3_errmsg(ctx->db));
            sqlite3_exec(ctx->db, "ROLLBACK", NULL, NULL, NULL);
            return -1;
        }
    }

    /* Commit transaction */
    sqlite3_exec(ctx->db, "COMMIT", NULL, NULL, NULL);

    return 0;
}

int cache_dir_invalidate(cache_meta_ctx_t *ctx, const char *path)
{
    if (ctx == NULL || path == NULL) {
        return -1;
    }

    sqlite3_reset(ctx->delete_dir_stmt);
    sqlite3_bind_text(ctx->delete_dir_stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_step(ctx->delete_dir_stmt);

    return 0;
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

    if (ctx->insert_meta_stmt) {
        sqlite3_finalize(ctx->insert_meta_stmt);
    }
    if (ctx->select_meta_stmt) {
        sqlite3_finalize(ctx->select_meta_stmt);
    }
    if (ctx->delete_meta_stmt) {
        sqlite3_finalize(ctx->delete_meta_stmt);
    }
    if (ctx->insert_dir_stmt) {
        sqlite3_finalize(ctx->insert_dir_stmt);
    }
    if (ctx->select_dir_stmt) {
        sqlite3_finalize(ctx->select_dir_stmt);
    }
    if (ctx->delete_dir_stmt) {
        sqlite3_finalize(ctx->delete_dir_stmt);
    }
    if (ctx->db) {
        sqlite3_close(ctx->db);
    }
    
    free(ctx->cache_root);
    free(ctx);

    DPRINTF("cache_meta_destroy: metadata cache destroyed");
}
