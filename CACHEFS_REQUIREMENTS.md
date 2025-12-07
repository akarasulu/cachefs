# 📘 **CacheFS Requirements Specification (Fork of bindfs)**

Version 1.0  
Author: Alex Karasulu  
Purpose: Transform bindfs into CacheFS — a persistent, metadata-aware, block-caching filesystem for SMB/NFS and other backends.

---

## 1\. Overview

CacheFS is a **fork of bindfs** that adds:

1. **Persistent metadata caching**
    
2. **Persistent data (block-based) read caching**
    
3. **Backend-agnostic read-through behavior** (SMB, NFS, WebDAV, or any POSIX path)
    
4. **Write-through semantics** (writes always hit backend immediately)
    
5. **Coherency via mtime/size revalidation**
    
6. **Optional backend-driven event invalidation for SMB** via embedded SMB client and SMB2/3 `CHANGE_NOTIFY`
    
7. **Bindfs-style remapping** (ownership, permissions, volname, etc.)
    

CacheFS **does not** implement or require Linux FS-Cache.  
CacheFS **emulates the behavior of cachefilesd**, except it performs caching itself rather than via kernel APIs.

---

## 2\. High-level Architecture

CacheFS consists of:

- **FUSE frontend**
    
    - Intercepts open/readdir/read/write/getattr/etc.
        
- **Backend access module**
    
    - Reads/writes from the real filesystem path (SMB/NFS mount), or
        
    - Uses an embedded SMB client (optional)
        
- **Metadata cache subsystem (SQLite or LMDB)**
    
- **Block cache subsystem (flat files, hashed path structure)**
    
- **Directory cache subsystem**
    
- **Invalidation engine**
    
- **Optional SMB Change-Notify subsystem**
    
- **Bindfs compatibility layer** (remapping, volname, uid/gid translation)
    

---

## 3\. Mount Cardinality Requirements

### 3.1 One cache instance per mount

Each CacheFS mount creates and maintains its own:

- block cache root
    
- metadata DB
    
- notification state
    
- directory watch list
    

No shared cache across mounts for v1.

### 3.2 Cache root directory

Default:

bash

Copy code

`~/.cache/cachefs/<mountid>/     meta.db     blocks/     dirs/     neg/`

Customizable via `--cache-root=`.

---

## 4\. Backend Model Requirements

CacheFS may operate in one of two backend modes:

### 4.1 POSIX passthrough backend (default)

- CacheFS passes through operations to an existing mount (SMB/NFS/WebDAV).
    
- Backend path is provided as bindfs does (`realpath` concept).
    

### 4.2 Embedded SMB client backend (optional)

- CacheFS directly speaks SMB2/3 using a client library (libsmb2 preferred).
    
- Used for:
    
    - Direct I/O
        
    - SMB CHANGE\_NOTIFY events
        

If this mode is disabled, CacheFS behaves as a caching layer on top of macOS SMBFS.

---

## 5\. Caching Requirements

### 5.1 Write behavior

- **No write-back mode**
    
- Only write-through semantics supported
    
- Remove any `--cache-write` flag
    
- Writes always go directly to backend, then invalidate/update cache entries
    

### 5.2 Metadata caching

Cache the results of:

- `stat` / `lstat`
    
- directory listings (`readdir`)
    
- negative lookups (ENOENT for files)
    

#### Metadata DB fields:

pgsql

Copy code

`path                TEXT PRIMARY KEY type                FILE/DIR/NEG size                INTEGER mtime               INTEGER ctime               INTEGER mode                INTEGER uid, gid            INTEGER dir_generation      INTEGER NULLABLE cached_at           TIMESTAMP valid_until         TIMESTAMP backend_signature   BLOB OPTIONAL`

### 5.3 Directory caching

Store directory entries in metadata DB using:

bash

Copy code

`dir:PATH → list of child entries mtime cached_at valid_until`

### 5.4 Block data caching

Store cached file blocks under:

php-template

Copy code

`blocks/XX/YY/<hash>-<blockindex>`

Block size default: **256 KiB**.

---

## 6\. Coherency Requirements

### 6.1 Revalidation rules

CacheFS must revalidate entries based on:

- mtime comparison with backend
    
- size comparison with backend
    
- TTL expiration (configurable)
    
- Directory mtime change
    
- Invalidation events (SMB notify)
    

### 6.2 Open-time revalidation

On every `open()` call:

- Check cached stat vs backend stat
    
- If mismatch → invalidate file's blocks + metadata
    

### 6.3 Directory revalidation

On `readdir()`:

- Check cached directory mtime vs backend
    
- If mismatch → refresh entire directory listing
    

---

## 7\. SMB Notification Requirements (Optional)

If using embedded SMB backend:

### 7.1 Registration of notifications

- When a directory is first cached:
    
    - Issue SMB2 CHANGE\_NOTIFY
        
- When directory evicted:
    
    - Remove notify request
        

### 7.2 Notification events

On receiving events:

- Invalidate directory metadata cache
    
- Invalidate file metadata for changed files
    
- Invalidate affected blocks
    

### 7.3 Notification threading

- Use background thread or async loop
    
- Must not block FUSE handlers
    
- Must deliver invalidations quickly
    

---

## 8\. Bindfs Compatibility Layer

CacheFS must support the following bindfs features:

- UID/GID remap
    
- Mode remap
    
- Volname option for macOS Finder
    
- Symlink handling options
    
- Permission presentation options
    

All bindfs options remain, unless incompatible with caching.

---

## 9\. FUSE Operation Requirements

### 9.1 `getattr(path)`

- Check metadata cache
    
- Revalidate if needed
    
- Otherwise forward to backend
    
- Store stat in metadata DB
    

### 9.2 `readdir(path)`

- Consult directory cache
    
- Revalidate if needed
    
- Fetch backend children on cache miss
    
- Store directory cache in DB
    

### 9.3 `read(path, size, offset)`

- Determine block index
    
- If block cached → return bytes
    
- If not cached → read backend block, store, return
    

### 9.4 `write(path, size, offset)`

- Write-through:
    
    - Write to backend first
        
    - Update or invalidate block cache
        
    - Update metadata cache
        

### 9.5 Create/unlink/rename operations

- Always invalidate parent directory cache
    
- Always refresh metadata upon next `getattr`
    

---

## 10\. Cache Lifetime & TTL Requirements

### 10.1 Default TTLs

- Directory cache TTL: **5–20s** depending on size
    
- Metadata stat TTL: **5s**
    
- Negative entries: **2s**
    
- Block cache TTL: infinite unless file changed
    

### 10.2 Eviction rules

- LRU eviction based on cache directory size limit
    
- Limits configurable via mount options
    

---

## 11\. Logging & Instrumentation

CacheFS should log:

- cache hits vs misses
    
- block loads
    
- revalidations
    
- directory invalidations
    
- notification callbacks (if SMB notify enabled)
    

Add verbose logging flag `--cache-debug`.

---

## 12\. Error Handling Requirements

- On backend errors, propagate appropriate errno
    
- On cache DB corruption, fallback to passthrough mode
    
- On failed SMB notify registration, fallback to TTL-coherency
    

---

## 13\. CLI & Mount Options

CacheFS should support:

sql

Copy code

`--cache-root     path to cache directory --cache-meta-ttl=SECONDS --cache-dir-ttl=SECONDS --cache-neg-ttl=SECONDS --cache-block-size=BYTES --cache-max-size=BYTES --cache-debug --backend-smb     # optional, use embedded SMB client --smb-server=... --smb-share=... --smb-user=... --smb-pass=...`

Remove:

arduino

Copy code

`--cache-write`

Write-through is always used.

---

## 14\. Security Requirements

- Credentials should not be logged
    
- Protect cache directory with `0700` permissions
    
- Avoid storing plaintext credentials (use session tokens if possible)
    

---

## 15\. Performance Requirements

- Metadata lookup (cache hit): < 1ms
    
- Block read (cache hit): near local SSD speed
    
- SMB notify latency: < 50ms if backend supports it
    
- Directory cache miss: equivalent speed to backend read
    

---

## 16\. Out-of-Scope for v1

- Write-back caching
    
- Distributed cache sharing between multiple mounts
    
- NFSv4 callback server implementation
    
- Kernel integration
    
- FS-Cache compatibility
    

---

## 17\. Implementation Milestones

### Phase 1 – Fork bindfs & create baseline skeleton

- Rename to CacheFS
    
- Set up cache root per mount
    
- Integrate metadata DB
    
- Add block caching layer
    
- Replace passthrough read/write with caching logic
    

### Phase 2 – Metadata caching

- Implement stat/store/revalidate logic
    
- Integrate directory cache
    
- Add negative entry cache
    

### Phase 3 – Data block caching

- Implement block layout
    
- Add block fetch/store path
    
- Implement block LRU eviction
    

### Phase 4 – Coherency engine

- mtime/size revalidation
    
- Directory invalidation
    
- Cache invalidation on writes
    

### Phase 5 – Optional SMB client + notify integration

- Integrate libsmb2
    
- Issue notify requests per directory
    
- Invalidate metadata on notify events
    

### Phase 6 – Bindfs remap features

- UID/GID
    
- mode remap
    
- volname
    

### Phase 7 – CLI options & mount polish

- Add mount options
    
- Add logging
    
- Add debug mode
    
