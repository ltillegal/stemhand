#!/usr/bin/env bash
# Fetches the ONNX Runtime C++ headers and native library.
#
# The 176 MB NuGet package is not committed: it is a platform-specific binary
# blob, and git is the wrong place for it. macOS can also take the library from
# Homebrew; everywhere else this script unpacks the package into third_party/,
# where CMake already looks.
#
# Source is NuGet rather than the GitHub release, because the GitHub release
# assets have become unavailable — the repository returns 404 while the packages
# keep flowing. NuGet versions are immutable, so the version pin stays honest.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="1.32.0"

case "$(uname -s)" in
    Darwin)
        case "$(uname -m)" in
            arm64) platform="osx-arm64" ;;
            x86_64) platform="osx-x64" ;;
            *) echo "unsupported architecture: $(uname -m)" >&2; exit 1 ;;
        esac
        library_pattern="*.dylib"
        ;;
    Linux)
        case "$(uname -m)" in
            x86_64) platform="linux-x64" ;;
            aarch64) platform="linux-arm64" ;;
            *) echo "unsupported architecture: $(uname -m)" >&2; exit 1 ;;
        esac
        library_pattern="*.so*"
        ;;
    *)
        echo "unsupported platform: $(uname -s)" >&2
        exit 1
        ;;
esac

# Homebrew already provides a matching layout on macOS; prefer it when present.
if [ "$(uname -s)" = "Darwin" ] && command -v brew > /dev/null 2>&1; then
    prefix="$(brew --prefix onnxruntime 2>/dev/null || true)"
    if [ -n "$prefix" ] && [ -f "$prefix/include/onnxruntime_cxx_api.h" ]; then
        echo "$prefix"
        exit 0
    fi
fi

dest="$root/third_party/onnxruntime-$platform-$version"

# The package ships the library without its SONAME link, but the binary records
# the SONAME (libonnxruntime.so.1) at link time. Create the link the loader will
# look for; harmless when it already exists.
link_sonames() {
    if [ -f "$dest/lib/libonnxruntime.so" ]; then
        ln -sf libonnxruntime.so "$dest/lib/libonnxruntime.so.1.32.0"
        ln -sf libonnxruntime.so "$dest/lib/libonnxruntime.so.1"
    fi
}

if [ -d "$dest/lib" ] && [ -d "$dest/include" ]; then
    link_sonames
    echo "$dest"
    exit 0
fi

if ! command -v unzip > /dev/null 2>&1; then
    echo "unzip is required to unpack the ONNX Runtime package" >&2
    exit 1
fi

url="https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/$version/microsoft.ml.onnxruntime.$version.nupkg"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "downloading onnxruntime $version ($platform) from NuGet ..." >&2
curl -fsSL --retry 3 --retry-delay 2 -o "$tmp/ort.nupkg" "$url"

mkdir -p "$tmp/unpacked"
unzip -q "$tmp/ort.nupkg" -d "$tmp/unpacked"

mkdir -p "$dest/include" "$dest/lib"
cp "$tmp/unpacked"/build/native/include/*.h "$dest/include/"

# shellcheck disable=SC2086
cp -a "$tmp/unpacked/runtimes/$platform/native/"$library_pattern "$dest/lib/"
link_sonames

echo "$dest"
