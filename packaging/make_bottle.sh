#!/usr/bin/env bash
set -euo pipefail

version="${1:-${PACKAGE_VERSION:-unknown}}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
formula_path="${root_dir}/Formula/cachefs.rb"
dist_dir="${root_dir}/dist/bottles"
tap_name="local/cachefs"
# create a temporary tap so Homebrew accepts the formula location
tmp_tap="$(mktemp -d "${TMPDIR:-/tmp}/cachefs-tap-XXXXXX")"
tap_url="file://${tmp_tap}"

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required to build bottles" >&2
  exit 1
fi

mkdir -p "${dist_dir}"
mkdir -p "${tmp_tap}/Formula"
cp "${formula_path}" "${tmp_tap}/Formula/"

# Homebrew expects taps to be git repos
pushd "${tmp_tap}" >/dev/null
git init -q
git config user.email "cachefs@example.com"
git config user.name "cachefs"
git add Formula/cachefs.rb
git commit -q -m "Add cachefs formula"
popd >/dev/null

pushd "${root_dir}" >/dev/null

echo ">> Creating tap ${tap_name} from ${tap_url}"
brew untap "${tap_name}" >/dev/null || true
brew tap "${tap_name}" "${tap_url}" --force-auto-update

echo ">> Installing for bottling"
brew install --build-bottle "${tap_name}/cachefs"

echo ">> Bottling cachefs (root-url=file://${dist_dir})"
brew bottle --json --root-url="file://${dist_dir}" "${tap_name}/cachefs"

echo ">> Moving bottle artifacts to ${dist_dir}"
find . -maxdepth 1 -name "cachefs--*.tar.gz" -o -name "cachefs--*.bottle.json" | while read -r f; do
  mv "$f" "${dist_dir}/"
done

echo ">> Bottle build complete. Artifacts in ${dist_dir}"

popd >/dev/null
