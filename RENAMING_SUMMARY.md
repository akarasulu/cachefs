# CacheFS Renaming Summary

All functional `bindfs` references have been updated to `cachefs` throughout the codebase.

## Files Renamed

- `tests/test_bindfs.rb` → `tests/test_cachefs.rb`

## Updated References

### Source Files
- **Header guards**: All `INC_BINDFS_*` → `INC_CACHEFS_*`
  - `src/debug.h`
  - `src/misc.h`
  - `src/userinfo.h`
  - `src/arena.h`
  - `src/permchain.h`
  - `src/rate_limiter.h`
  - `src/usermap.h`
  - `tests/internals/test_common.h`

- **Debug macros**: `BINDFS_DEBUG` → `CACHEFS_DEBUG`
  - `src/debug.h`
  - `src/permchain.c`

### Test Files
- **Variable names**: `bindfs_pid` → `cachefs_pid`, `bindfs_args` → `cachefs_args`
  - `tests/common.rb`
  - `tests/test_cachefs.rb`
  
- **Class names**: `BindfsRunner` → `CachefsRunner`
  - `tests/stress_test.rb`

- **Test directory**: `tmp_test_bindfs` → `tmp_test_cachefs`
  - `tests/common.rb`

- **Test groups**: `bindfs_test_group` → `cachefs_test_group`
  - `tests/test_cachefs.rb`

- **Mount test**: `_bindfs_test_123_` → `_cachefs_test_123_`
  - `tests/test_cachefs.rb`

### Documentation
- **Man page**: Updated from BINDFS to CACHEFS
  - `src/cachefs.1`

- **Build files**:
  - `tests/Makefile.am`: Updated test reference
  - `.github/copilot-instructions.md`: Updated all function names and file references
  - `README.md`: Updated test command example

## Preserved References

The following `bindfs` references were **intentionally preserved** as they refer to the original project:

- Copyright headers: "This file is part of bindfs"
- License text: "bindfs is free software..."
- Historical comments and attribution
- External URLs: https://bindfs.org/, https://github.com/mpartel/bindfs
- Man page examples showing bindfs commands for reference
- README.bindfs.md (original documentation)

## Verification

Run `make check` to verify all tests pass with the new naming:
```bash
./test-all.sh
```

All 23+ tests should continue to pass with the new `cachefs` naming throughout.
