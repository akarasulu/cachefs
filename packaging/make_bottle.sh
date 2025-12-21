#!/usr/bin/env bash
set -euo pipefail

version="${1:-${PACKAGE_VERSION:-unknown}}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
formula_path="${root_dir}/Formula/cachefs.rb"
dist_dir="${root_dir}/dist/bottles"

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required to build bottles" >&2
  exit 1
fi

mkdir -p "${dist_dir}"

pushd "${root_dir}" >/dev/null

echo ">> Installing for bottling"
brew install --build-bottle "${formula_path}"

echo ">> Bottling cachefs (root-url=file://${dist_dir})"
brew bottle --json --root-url="file://${dist_dir}" cachefs

echo ">> Moving bottle artifacts to ${dist_dir}"
find . -maxdepth 1 -name "cachefs--*.tar.gz" -o -name "cachefs--*.bottle.json" | while read -r f; do
  mv "$f" "${dist_dir}/"
done

echo ">> Bottle build complete. Artifacts in ${dist_dir}"

popd >/dev/null
