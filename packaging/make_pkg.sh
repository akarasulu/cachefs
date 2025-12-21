#!/usr/bin/env bash
set -euo pipefail

version="${1:-${PACKAGE_VERSION:-unknown}}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dist_dir="${root_dir}/dist"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/cachefs-pkg-XXXXXX")"
pkg_root="${work_dir}/root"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This target is macOS-only (requires pkgbuild)" >&2
  exit 1
fi

if ! command -v pkgbuild >/dev/null 2>&1; then
  echo "pkgbuild is required (part of Xcode Command Line Tools)" >&2
  exit 1
fi

mkdir -p "${pkg_root}" "${dist_dir}"

echo ">> Staging files with make install (DESTDIR=${pkg_root})"
make -C "${root_dir}" install DESTDIR="${pkg_root}"

pkg_path="${dist_dir}/cachefs-${version}.pkg"
identifier="org.cachefs.pkg"

echo ">> Building ${pkg_path}"
pkgbuild \
  --root "${pkg_root}" \
  --identifier "${identifier}" \
  --version "${version}" \
  --install-location / \
  "${pkg_path}"

echo ">> macOS package written to ${pkg_path}"
