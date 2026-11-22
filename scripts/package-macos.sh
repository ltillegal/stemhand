#!/usr/bin/env bash
# Builds a macOS installer package (.pkg) from the app bundle.
#
# Run scripts/build-macos.sh first: this only wraps an existing bundle.
# `pkgbuild` exists only on macOS.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app="$root/build-macos/stemhand.app"

if [ "$(uname -s)" != "Darwin" ]; then
    echo "pkgbuild only runs on macOS" >&2
    exit 1
fi

if [ ! -d "$app" ]; then
    echo "no app bundle at $app — run scripts/build-macos.sh first" >&2
    exit 1
fi

version="$("$app/Contents/MacOS/stemhand" --version | awk '{print $2}')"
stage="$root/build-macos/pkg-root"

rm -rf "$stage"
mkdir -p "$stage"
# The payload is rooted at /Applications, so the bundle lands where a user
# expects an application to be.
cp -R "$app" "$stage/"

package="$root/build-macos/stemhand-$version-macos-arm64.pkg"
pkgbuild \
    --root "$stage" \
    --identifier dev.stemhand.app \
    --version "$version" \
    --install-location /Applications \
    "$package"

rm -rf "$stage"
echo "$package"
