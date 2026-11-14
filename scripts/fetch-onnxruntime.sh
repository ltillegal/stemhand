#!/usr/bin/env bash
# Fetches the ONNX Runtime C++ release the build expects.
#
# The 35 MB archive is not committed: it is a platform-specific binary blob, and
# git is the wrong place for it. macOS takes it from Homebrew; everywhere else
# this script drops it into third_party/ where CMake already looks.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="1.32.0"

case "$(uname -s)" in
    Darwin)
        prefix="$(brew --prefix onnxruntime 2>/dev/null || true)"
        if [ -z "$prefix" ] || [ ! -d "$prefix" ]; then
            echo "brew install onnxruntime" >&2
            exit 1
        fi
        echo "$prefix"
        exit 0
        ;;
    Linux)
        platform="linux-x64"
        ;;
    *)
        echo "unsupported platform: $(uname -s)" >&2
        exit 1
        ;;
esac

dest="$root/third_party/onnxruntime-$platform-$version"
if [ -d "$dest" ]; then
    echo "$dest"
    exit 0
fi

url="https://github.com/microsoft/onnxruntime/releases/download/v$version/onnxruntime-$platform-$version.tgz"
mkdir -p "$root/third_party"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "downloading onnxruntime $version ..." >&2
curl -sSL -o "$tmp/ort.tgz" "$url"
tar xzf "$tmp/ort.tgz" -C "$root/third_party"
echo "$dest"
