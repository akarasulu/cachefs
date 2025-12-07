# CacheFS Development Guide

CacheFS is a **fork of bindfs** that adds persistent metadata and block-level caching to FUSE filesystems. This guide covers architecture, development patterns, and key implementation points.

## Project Context

- **Base**: bindfs 1.18.3 - a FUSE filesystem for remapping directories with modified permissions/ownership
- **Goal**: Add persistent caching layer while preserving all bindfs features (UID/GID/permission remapping)
- **Use case**: Cache remote filesystems (SMB/NFS) with write-through semantics
- **Language**: C (C99/gnu11), targeting FUSE 2.8+ and FUSE 3.x

## Build System

Autotools-based build using `autogen.sh` → `configure` → `make`:

```bash
./autogen.sh              # Generate configure (only from git clone)
./configure               # Detect FUSE version, set HAVE_FUSE_3
make                      # Build src/cachefs
make check                # Run Ruby test suite (as user)
sudo make check           # Run privileged tests
```

**Key files**: `configure.ac` (build config), `src/Makefile.am` (source list), `tests/Makefile.am` (test suite)

## Architecture Overview

### FUSE Operation Flow

All filesystem operations follow this pattern in `src/cachefs.c`:

1. **Path translation**: `process_path()` converts FUSE virtual path → real backend path
2. **Backend operation**: Forward to underlying filesystem via syscalls
3. **Attribute remapping**: Apply UID/GID/permission transformations in `getattr_common()`
4. **Return to FUSE**: Results passed back through FUSE callback

**Critical FUSE callbacks** (line 1751, `struct fuse_operations cachefs_oper`):
- `cachefs_getattr()` - stat operations, remaps ownership/permissions
- `cachefs_readdir()` - directory listing (line 824)
- `cachefs_open()` - file open (line 1366)
- `cachefs_read()` - read data (line 1392)
- `cachefs_write()` - write data (line 1431)
- `cachefs_create()` - file creation (line 1308)

### Key Data Structures

```c
// Global settings (line ~120)
static struct Settings {
    char *mntsrc;              // Backend path
    char *mntdest;             // Mount point
    uid_t new_uid, new_gid;    // Forced ownership
    UserMap *usermap;          // UID/GID remapping
    struct permchain *permchain; // Permission rules
    RateLimiter *read_limiter, *write_limiter;
    // ... many more options
} settings;
```

### Existing Subsystems

- **`usermap.c/h`**: UID/GID mapping with dynamic arrays and lookup functions
- **`permchain.c/h`**: Permission rules parser (chmod-like syntax: `og-x,u=rwX`)
- **`rate_limiter.c/h`**: Bandwidth throttling for reads/writes
- **`arena.c/h`**: Block allocator for temporary path strings
- **`userinfo.c/h`**: User/group name resolution
- **`misc.c/h`**: Helper functions, string utilities
- **`debug.c/h`**: `DPRINTF()` macro for `--debug` mode

## CacheFS Implementation Strategy

### 1. Add Cache Infrastructure

**New modules needed**:
- `cache_meta.c/h` - LMDB metadata cache (fast, embedded key-value store)
  - Key: file path (string)
  - Value: struct with {type (FILE/DIR/NEG), size, mtime, ctime, mode, uid, gid, cached_at, valid_until}
  - Separate DB for directory entries: key=dir_path+entry_name, value=entry_type
  - Use `mdb_env_open()`, `mdb_txn_begin()`, `mdb_get()`, `mdb_put()` for operations
- `cache_block.c/h` - Block storage under `~/.cache/cachefs/<mountid>/blocks/XX/YY/<hash>-<blockindex>`
- `cache_coherency.c/h` - Revalidation logic (mtime/size comparison)
- ~~`cache_smb.c/h`~~ - SMB client integration deferred to post-v1

### 2. Injection Points for Caching

**In `cachefs_getattr()` (line ~752)**:
```c
// BEFORE backend stat():
if (cache_meta_lookup(path, &cached_stat, &valid)) {
    if (valid || cache_revalidate(path, &cached_stat)) {
        *stbuf = cached_stat;
        return 0;  // Cache hit
    }
}
// AFTER backend stat():
cache_meta_store(path, stbuf);
```

**In `cachefs_readdir()` (line 824)**:
```c
// Check directory cache validity by mtime
if (cache_dir_lookup(path, &entries, &dir_mtime)) {
    struct stat backend_st;
    if (stat(real_path, &backend_st) == 0 && 
        backend_st.st_mtime == dir_mtime) {
        return cached_entries;  // Cache hit
    }
}
// After readdir(), store entries:
cache_dir_store(path, entries, dir_mtime);
```

**In `cachefs_read()` (line 1392)**:
```c
// Calculate block index/offset
size_t block_idx = offset / BLOCK_SIZE;
size_t block_offset = offset % BLOCK_SIZE;

if (cache_block_exists(path, block_idx)) {
    return cache_block_read(path, block_idx, buf, size, block_offset);
}
// On miss: read from backend, write to cache
ssize_t bytes = pread(fi->fh, buf, size, offset);
cache_block_write(path, block_idx, buf, bytes);
return bytes;
```

**In `cachefs_write()` (line 1431)**:
```c
// Write-through: backend first
ssize_t res = pwrite(fi->fh, buf, size, offset);
if (res > 0) {
    // Invalidate affected blocks
    cache_block_invalidate(path, offset, size);
    // Update metadata (size may have changed)
    cache_meta_invalidate(path);
}
```

**In `cachefs_open()` (line 1366)**:
```c
// Revalidation on open
struct stat backend_st, cached_st;
if (cache_meta_lookup(path, &cached_st, NULL)) {
    if (fstat(fd, &backend_st) == 0) {
        if (backend_st.st_mtime != cached_st.st_mtime ||
            backend_st.st_size != cached_st.st_size) {
            cache_invalidate_file(path);  // Invalidate all blocks + metadata
        }
    }
}
```

### 3. Mount Options

Add to `print_usage()` and option parsing (line ~1850):
```
--cache-root=PATH         Cache directory (default: ~/.cache/cachefs/<mountid>)
--cache-meta-ttl=SECS     Metadata TTL (default: 5)
--cache-dir-ttl=SECS      Directory TTL (default: 10)
--cache-block-size=BYTES  Block size (default: 262144 = 256KB)
--cache-max-size=BYTES    Max cache size for LRU eviction
--cache-debug             Enable cache hit/miss logging
```

Note: SMB backend options deferred to post-v1.

### 4. Testing Strategy

Add cache-specific tests **incrementally** as features are built. Extend `tests/test_cachefs.rb`:
```ruby
testenv("--cache-root=/tmp/cachefs-test") do
  touch('src/file')
  # First read - cache miss
  read('mnt/file')
  # Second read - cache hit
  read('mnt/file')
  # Verify cache hit in debug log
end
```

Run tests: `./test-all.sh` (includes valgrind if available)

**Testing approach**: Add 2-3 tests per feature (e.g., metadata cache tests when adding `cache_meta.c`, block cache tests when adding `cache_block.c`). Ensure existing bindfs tests continue to pass.

## Development Conventions

### Error Handling
- Return negative errno: `return -errno;` or `return -EINVAL;`
- Always `free()` allocated paths before returning on error
- Check `process_path()` return: `if (real_path == NULL) return -errno;`

### Memory Management
- Use `arena_malloc()` for temporary allocations freed together (see `arena.h`)
- `process_path()` returns malloc'd string - **caller must free()**
- Strdup all paths before modifying

### FUSE Version Compatibility
- Use `#ifdef HAVE_FUSE_3` for FUSE 3-specific code
- FUSE 3 passes `struct fuse_file_info *fi` to more callbacks (getattr, chmod, etc.)
- Check `configure.ac` line 16 for FUSE version detection

### Logging
- Use `DPRINTF("format", ...)` for debug output (enabled with `--enable-debug-output` at configure time)
- Only logs when `--debug` runtime flag set
- Add cache metrics: `DPRINTF("Cache hit: %s", path);`

### Platform Differences
- macOS: uses fuse-t, has special xattr handling (`__APPLE__` sections)
- Linux: supports `O_DIRECT` flag forwarding (`__linux__`)
- FreeBSD: limited FUSE support

## Critical Files to Modify

1. **`src/cachefs.c`** (3002 lines) - Main FUSE operations, add cache lookups
2. **`src/Makefile.am`** - Add new cache source files
3. **`configure.ac`** - Add liblmdb detection with `PKG_CHECK_MODULES([LMDB], [lmdb])`
4. **`tests/test_cachefs.rb`** - Add cache validation tests incrementally

## Don't Break

- **All existing bindfs features** must work identically (UID/GID remap, permission chains, rate limiting)
- **Test suite** must pass: `./test-all.sh` and `sudo ./test-all.sh`
- **FUSE 2 and 3 compatibility** - guard new code with version checks
- **Single-threaded mode** - avoid recursive mount access (checked in `process_path()`)

## Helpful Commands

```bash
# Build and run locally
./autogen.sh && ./configure && make
./src/cachefs --help

# Mount example
mkdir /tmp/src /tmp/mnt
./src/cachefs /tmp/src /tmp/mnt
ls -la /tmp/mnt
fusermount -u /tmp/mnt  # unmount (Linux)
umount /tmp/mnt         # unmount (macOS)

# Debug mode
./src/cachefs --debug /tmp/src /tmp/mnt

# Run specific test
cd tests && ./test_cachefs.rb --test=test_basic_mount
```
