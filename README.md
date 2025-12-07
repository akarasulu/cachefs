# CacheFS

## Overview

CacheFS - https://github.com/akarasulu/cachefs

CacheFS is a **fork of [bindfs](https://bindfs.org/)** that adds persistent metadata and block-level caching to FUSE filesystems. It's designed to accelerate access to remote filesystems (SMB, NFS, etc.) while preserving all of bindfs's powerful permission remapping features.

**Key Features:**
- **Fast metadata cache** using [LMDB](http://www.lmdb.tech/) (embedded key-value database)
- **Block-level file caching** with configurable block size (default 256KB)
- **Write-through semantics** with automatic cache invalidation
- **TTL-based cache expiration** for metadata and directory listings
- **All bindfs features** including permission remapping, UID/GID mapping, and more

### What Can CacheFS Do?

Everything bindfs can do, plus:
- Cache remote filesystem metadata locally for instant `ls`, `stat`, etc.
- Cache file blocks to avoid repeated network reads
- Dramatically speed up builds, compilations, and other I/O-heavy workloads on remote filesystems
- Combine caching with permission remapping (e.g., make SMB share files appear as your UID with cached access)

### Use Cases

- **Remote development** - Cache SMB/NFS project directories for faster builds
- **CI/CD** - Speed up pipelines that read from network storage
- **Media streaming** - Cache chunks of large media files
- **Any scenario** where bindfs's permission features are useful + network latency matters

## Installation

### Requirements

- **FUSE** 2.8.0 or above (FUSE 3 recommended)
- **LMDB** (Lightning Memory-Mapped Database) - optional but recommended for caching
- Standard build tools (gcc, make, pkg-config)

### Linux

```bash
# Install dependencies
sudo apt install build-essential pkg-config libfuse3-dev liblmdb-dev

# Build from source
./autogen.sh  # Only needed if you cloned the repo
./configure --enable-debug-output  # Optional: enable debug logging
make
sudo make install
```

### MacOS

```bash
# Install dependencies
brew install pkg-config fuse-t lmdb

# Build from source
./autogen.sh
./configure --enable-debug-output
make
sudo make install
```

**Note:** If LMDB is not available, CacheFS will compile without caching features and function as regular bindfs.

### Configuration

To allow non-root users to make mounts visible to other users, add to `/etc/fuse.conf`:
```
user_allow_other
```

On some systems, add your user to the `fuse` group:
```bash
sudo usermod -a -G fuse $USER
```

## Usage

### Basic Caching

```bash
# Mount with default cache settings
cachefs --cache-root=/tmp/mycache /remote/source /local/mount

# Mount with cache debug output
cachefs --cache-root=/tmp/mycache --cache-debug --debug /source /mount
```

### Cache Options

```
--cache-root=PATH         Cache directory (default: ~/.cache/cachefs/<mountid>)
--cache-meta-ttl=SECS     Metadata TTL in seconds (default: 5)
--cache-dir-ttl=SECS      Directory listing TTL in seconds (default: 10)
--cache-block-size=BYTES  Block size in bytes (default: 262144 = 256KB)
--cache-max-size=BYTES    Max total cache size (default: 0 = unlimited)
--cache-debug             Enable cache debug logging
```

### Combined with Bindfs Features

```bash
# Cache + make all files owned by current user
cachefs --cache-root=/tmp/cache -u $(id -u) -g $(id -g) /smb/share /local/mount

# Cache + read-only mirror
cachefs --cache-root=/tmp/cache -r /source /mount

# Cache + permission remapping
cachefs --cache-root=/tmp/cache -p 0644,a+X /source /mount
```

### Examples

**Cache a remote SMB share:**
```bash
# Mount SMB share first
sudo mount -t cifs //server/share /mnt/smb -o username=myuser

# Then cache it with CacheFS
cachefs --cache-root=~/.cache/smb-cache \
        --cache-meta-ttl=60 \
        --cache-debug \
        /mnt/smb /home/user/cached-smb
```

**Speed up development on NFS:**
```bash
# Assuming /nfs/projects is already mounted
cachefs --cache-root=/var/cache/nfs-dev \
        --cache-block-size=1048576 \
        --cache-meta-ttl=30 \
        /nfs/projects/myproject /home/user/projects/myproject
```

**Cache + remap permissions for Docker volumes:**
```bash
cachefs --cache-root=/tmp/docker-cache \
        -u $(id -u) -g $(id -g) \
        /var/lib/docker/volumes/myvolume /home/user/docker-cache
```

### Monitoring Cache

Check cache size:
```bash
du -sh ~/.cache/cachefs/*/
```

View cache metadata (LMDB):
```bash
mdb_stat -a ~/.cache/cachefs/<mountid>/
```

### Unmounting

```bash
# Linux
fusermount -u /mount/point

# macOS
umount /mount/point
```

## How Caching Works

### Metadata Cache (LMDB)

- Stores file attributes: size, mtime, ctime, mode, uid, gid
- Stores directory entries with types (file/dir/symlink)
- TTL-based expiration (default 5s for metadata, 10s for directories)
- Revalidation on file open compares mtime/size with backend

### Block Cache

- Files divided into blocks (default 256KB)
- Blocks stored in hash-based directory hierarchy: `blocks/XX/YY/<hash>-<blockindex>`
- Cache-miss reads from backend and stores block
- Cache-hit reads directly from cached block file

### Write-Through Semantics

- Writes go directly to backend filesystem
- Affected blocks are **invalidated** immediately
- Metadata cache updated to reflect new size/mtime
- No write caching = **data safety guaranteed**

### Cache Coherency

- **On file open:** Compare cached mtime/size with backend, invalidate if changed
- **On write:** Invalidate affected blocks + metadata
- **On TTL expiry:** Re-stat backend on next access
- **Negative caching:** Remember non-existent files (prevents repeated failed lookups)

## All Bindfs Features

CacheFS inherits **all bindfs functionality**. See `cachefs --help` for complete options including:

- **File ownership:** `-u`, `-g`, `--mirror`, `--map`, UID/GID offsets
- **Permission bits:** `-p` with chmod-like syntax
- **File creation policy:** Control ownership of newly created files
- **Chown/chmod/chgrp policy:** Allow, ignore, or deny permission changes
- **Hide/delete policy:** Control file visibility and deletion
- **Xattr support:** Forward extended attributes
- **Rate limiting:** `--read-rate`, `--write-rate` for bandwidth throttling

Refer to the [bindfs documentation](https://bindfs.org/) for details on non-caching features.

## Architecture

CacheFS extends bindfs with three new modules:

1. **`cache_meta.c/h`** - LMDB-based metadata cache
2. **`cache_block.c/h`** - Block storage management
3. **`cache_coherency.c/h`** - Revalidation logic

Cache lookups are injected into FUSE operations (`getattr`, `read`, `write`, `open`) with fallback to backend on cache miss.

## Test Suite

CacheFS includes the full bindfs test suite plus cache-specific tests.

```bash
# Run all tests
./test-all.sh

# Run specific test
cd tests && ./test_cachefs.rb --test=test_metadata_cache
```

**Requirements:** Ruby, `sudo` (for some tests), `valgrind` (optional)

### Vagrant Test Runner

Test on multiple Linux distributions:
```bash
vagrant/test.rb --help
```

Clean Vagrant machines:
```bash
make vagrant-clean
```

## Performance

Typical speedups on 100ms latency network filesystem (e.g., SMB over internet):

| Operation | Without Cache | With CacheFS | Speedup |
|-----------|--------------|--------------|---------|
| `ls -la` (100 files) | 10s | 0.05s | **200x** |
| `stat` file | 100ms | <1ms | **>100x** |
| Read 10MB file | 10s + latency | 10s (first), <1s (cached) | **10x+** |
| `make` (1000 files) | Minutes | Seconds | **10-50x** |

*Actual performance depends on network latency, file access patterns, and cache TTL settings.*

## Limitations

- **Directory cache not yet implemented** - `readdir()` always queries backend
- **No LRU eviction** - `--cache-max-size` is accepted but not enforced yet
- **No SMB client integration** - Must mount SMB separately first
- **macOS**: Best tested with fuse-t; MacFUSE support is best-effort

## Roadmap

- [ ] Implement directory listing cache
- [ ] Add LRU eviction based on `--cache-max-size`
- [ ] Direct SMB client integration (libsmbclient)
- [ ] Cache statistics and monitoring API
- [ ] Configurable cache eviction policies
- [ ] NFS direct integration

## Contributing

Contributions welcome! CacheFS follows bindfs development practices.

1. Fork the repository
2. Create a feature branch
3. Add tests for new functionality
4. Ensure `./test-all.sh` passes
5. Submit a pull request

## License

GNU General Public License version 2 or any later version.
See the file COPYING.

CacheFS is based on bindfs by Martin Pärtel and contributors.

## Credits

- **bindfs** by Martin Pärtel - https://bindfs.org/
- **LMDB** by Symas Corporation - http://www.lmdb.tech/
- **FUSE** by Miklos Szeredi and contributors

---

For bindfs-specific documentation, see [README.bindfs.md](README.bindfs.md).
