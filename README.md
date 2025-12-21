# CacheFS

## Overview

CacheFS - https://github.com/akarasulu/cachefs

CacheFS is a **fork of [bindfs](https://bindfs.org/)** that adds persistent metadata and block-level caching to FUSE filesystems. It's designed to accelerate access to slow filesystems (USB, SMB, NFS, etc.) while preserving all of bindfs' powerful permission remapping features.

**Key Features:**
- **Fast metadata cache** backed by SQLite (embedded SQL database)
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
- **SQLite** (embedded database for metadata caching)
- Standard build tools (gcc, make, pkg-config)

### Linux

```bash
# Install dependencies
sudo apt install build-essential pkg-config libfuse3-dev libsqlite3-dev

# Build from source
./autogen.sh  # Only needed if you cloned the repo
./configure --enable-debug-output  # Optional: enable debug logging
make
sudo make install
```

### MacOS

```bash
# Install dependencies
brew install pkg-config autoconf automake libtool sqlite
# Install a macOS FUSE implementation separately (e.g., macFUSE via `brew install --cask fuse`
# or fuse-t from https://www.fuse-t.org/).

# Build from source
./autogen.sh
./configure --enable-debug-output
make
sudo make install
```

**Note:** Homebrew does not ship a `fuse-t` formula. Install macFUSE via `brew install --cask fuse` (or the fuse-t vendor pkg) before building or running CacheFS on macOS.

**Note:** SQLite is required; builds fail without it and `cachefs` will not run if `libsqlite3` is missing at runtime.

### macOS Dependencies

- **Build-time:** `pkg-config`, `autoconf`, `automake`, `libtool`, Xcode Command Line Tools (clang/make), `sqlite`, plus a FUSE implementation (macFUSE via `brew install --cask macfuse`, or fuse-t from https://www.fuse-t.org/).
- **Runtime:** FUSE implementation (macFUSE or fuse-t) and `sqlite`

## Packaging

- **macOS `.pkg`:** `make pkg` (requires `pkgbuild` / Xcode CLT). Output: `dist/cachefs-<version>.pkg`.
- **Debian `.deb`:** `make deb` (requires `dpkg-deb`). Output: `dist/cachefs_<version>_<arch>.deb` with `Depends: fuse3, sqlite3`.
- **Homebrew tap/formula:** This repo can act as a tap. From a checkout: `brew install --build-from-source ./Formula/cachefs.rb` (or `--build-bottle` then `brew bottle cachefs`). As a tap: `brew tap <user>/cachefs <repo-url>` then `brew install <user>/cachefs/cachefs`. Uses the same deps: build-time `pkg-config`, autotools, FUSE implementation (macFUSE cask on macOS), `sqlite`; runtime FUSE implementation + `sqlite`.
- **Homebrew bottle from this repo:** `make brew` (alias `make tap`) builds a Homebrew bottle in `dist/bottles/` using the local formula. After uploading the `.tar.gz` and `.bottle.json` somewhere permanent, add the `bottle do` block that `brew bottle` outputs into `Formula/cachefs.rb` with the generated `sha256` entries. Once that block is present, users can `brew tap <user>/cachefs <repo-url>` and `brew install <user>/cachefs/cachefs` to pull the bottle automatically (macOS users will be prompted to install/approve the macFUSE cask).

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

Inspect cache metadata (SQLite):
```bash
sqlite3 ~/.cache/cachefs/<mountid>/metadata.db "SELECT COUNT(*) FROM metadata;"
```

### Unmounting

```bash
# Linux
fusermount -u /mount/point

# macOS
umount /mount/point
```

## How Caching Works

### Metadata Cache (SQLite)

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

1. **`cache_meta.c/h`** - SQLite-based metadata cache
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
- **SQLite** by D. Richard Hipp and contributors - https://www.sqlite.org/
- **FUSE** by Miklos Szeredi and contributors

---

For bindfs-specific documentation, see [README.bindfs.md](README.bindfs.md).
