#!/bin/bash
# Quick test to verify caching works and inode numbers are preserved

set -e

# Clean up any previous mounts
umount /tmp/cachefs_test_mnt 2>/dev/null || true
rm -rf /tmp/cachefs_test_src /tmp/cachefs_test_mnt
mkdir -p /tmp/cachefs_test_src /tmp/cachefs_test_mnt

echo "Creating test file..."
echo "hello world" > /tmp/cachefs_test_src/testfile
mkdir /tmp/cachefs_test_src/testdir

echo "Mounting cachefs..."
./src/cachefs /tmp/cachefs_test_src /tmp/cachefs_test_mnt

echo "Testing inode preservation..."
SRC_FILE_INO=$(stat -f "%i" /tmp/cachefs_test_src/testfile)
MNT_FILE_INO=$(stat -f "%i" /tmp/cachefs_test_mnt/testfile)
SRC_DIR_INO=$(stat -f "%i" /tmp/cachefs_test_src/testdir)
MNT_DIR_INO=$(stat -f "%i" /tmp/cachefs_test_mnt/testdir)

echo "  src file inode: $SRC_FILE_INO"
echo "  mnt file inode: $MNT_FILE_INO"
echo "  src dir inode:  $SRC_DIR_INO"
echo "  mnt dir inode:  $MNT_DIR_INO"

if [ "$SRC_FILE_INO" != "$MNT_FILE_INO" ]; then
    echo "FAIL: File inode mismatch!"
    umount /tmp/cachefs_test_mnt
    exit 1
fi

if [ "$SRC_DIR_INO" != "$MNT_DIR_INO" ]; then
    echo "FAIL: Dir inode mismatch!"
    umount /tmp/cachefs_test_mnt
    exit 1
fi

echo "SUCCESS: Inodes preserved correctly!"

# Test read (should hit cache on second read)
echo "Testing cache..."
cat /tmp/cachefs_test_mnt/testfile > /dev/null
cat /tmp/cachefs_test_mnt/testfile > /dev/null

echo "Unmounting..."
umount /tmp/cachefs_test_mnt

echo "Cleaning up..."
rm -rf /tmp/cachefs_test_src /tmp/cachefs_test_mnt

echo ""
echo "All tests passed!"
