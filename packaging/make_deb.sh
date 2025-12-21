#!/usr/bin/env bash
set -euo pipefail

version="${1:-${PACKAGE_VERSION:-unknown}}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dist_dir="${root_dir}/dist"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/cachefs-deb-XXXXXX")"
pkg_root="${work_dir}/pkgroot"
control_dir="${pkg_root}/DEBIAN"

if ! command -v dpkg-deb >/dev/null 2>&1; then
  echo "dpkg-deb is required to build the .deb package" >&2
  exit 1
fi

arch="$(dpkg --print-architecture 2>/dev/null || uname -m)"

mkdir -p "${pkg_root}" "${control_dir}" "${dist_dir}"

echo ">> Staging files with make install (DESTDIR=${pkg_root})"
make -C "${root_dir}" install DESTDIR="${pkg_root}"

cat > "${control_dir}/control" <<EOF
Package: cachefs
Version: ${version}
Section: utils
Priority: optional
Architecture: ${arch}
Depends: fuse3, sqlite3
Maintainer: CacheFS Maintainers <akarasulu@gmail.com>
Description: CacheFS FUSE caching filesystem with SQLite metadata store
EOF

deb_path="${dist_dir}/cachefs_${version}_${arch}.deb"

echo ">> Building ${deb_path}"
dpkg-deb --build "${pkg_root}" "${deb_path}"

echo ">> Debian package written to ${deb_path}"
