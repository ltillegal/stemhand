#!/usr/bin/env bash
# Checks whether this machine can build Stemhand, and says what is missing.
# Safe to run anywhere; nothing is installed or modified.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
missing=()

ok()   { printf '  ok    %s\n' "$1"; }
bad()  { printf '  MISS  %s\n' "$1"; missing+=("$1"); }
info() { printf '  ..    %s\n' "$1"; }

echo "system"
printf '  ..    %s %s\n' "$(uname -s)" "$(uname -m)"
if [ "$(uname -s)" = "Darwin" ]; then
    info "$(sw_vers -productVersion 2>/dev/null || echo 'unknown macOS version'), $(sysctl -n hw.ncpu) cores"
    [ "$(uname -m)" = "arm64" ] || bad "Apple Silicon (this is $(uname -m); CMake targets arm64)"
else
    info "$(nproc) cores, $(free -h | awk '/^Mem:/ {print $2}') RAM"
fi

echo "toolchain"
if command -v cmake > /dev/null; then
    version="$(cmake --version | head -1 | awk '{print $3}')"
    case "$version" in
        3.2[4-9]*|3.[3-9][0-9]*|[4-9].*) ok "cmake $version" ;;
        *) bad "cmake $version (need 3.24+)" ;;
    esac
else
    bad "cmake"
fi

if command -v c++ > /dev/null || command -v g++ > /dev/null || command -v clang++ > /dev/null; then
    ok "C++ compiler"
else
    bad "C++ compiler (xcode-select --install on macOS, base-devel on Arch)"
fi

if command -v pkg-config > /dev/null; then ok "pkg-config"; else bad "pkg-config"; fi

echo "dependencies"
if [ "$(uname -s)" = "Darwin" ]; then
    qt_prefix="$(brew --prefix qt 2>/dev/null || true)"
    ort_prefix="$(brew --prefix onnxruntime 2>/dev/null || true)"
else
    qt_prefix="$(pkg-config --variable=prefix Qt6Core 2>/dev/null || true)"
    ort_prefix=""
fi

if [ -n "$qt_prefix" ] && [ -f "$qt_prefix/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    ok "Qt at $qt_prefix"
else
    bad "Qt 6.5+ (brew install qt)"
fi

ort_found=""
for candidate in "$ort_prefix" "$root/third_party/onnxruntime-linux-x64-1.32.0"; do
    [ -n "$candidate" ] || continue
    if [ -f "$candidate/include/onnxruntime_cxx_api.h" ]; then
        ort_found="$candidate"
        break
    fi
done
if [ -n "$ort_found" ]; then
    ok "ONNX Runtime at $ort_found"
else
    info "ONNX Runtime missing — the app will build without the engine"
    info "  install with: brew install onnxruntime  or  ./scripts/fetch-onnxruntime.sh"
fi

if pkg-config --exists libsodium 2>/dev/null; then
    libdir="$(pkg-config --variable=libdir libsodium)"
    ok "libsodium $(pkg-config --modversion libsodium) ($libdir)"
    if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ]; then
        dylib="$libdir/libsodium.dylib"
        if [ -f "$dylib" ] && ! file "$dylib" | grep -q arm64; then
            bad "libsodium at $libdir is not arm64 — a stale Intel Homebrew is in the way"
        fi
    fi
else
    bad "libsodium (brew install libsodium)"
fi

for tool in ffmpeg ffprobe; do
    if command -v "$tool" > /dev/null; then
        ok "$tool ($(command -v "$tool"))"
    else
        bad "$tool (runtime dependency: brew install ffmpeg)"
    fi
done

echo "model store"
if [ "$(uname -s)" = "Darwin" ]; then
    store="$HOME/Library/Application Support/stemhand/models"
else
    store="${XDG_DATA_HOME:-$HOME/.local/share}/stemhand/models"
fi
if [ -d "$store" ]; then
    count="$(find "$store" -name manifest.json 2>/dev/null | wc -l)"
    info "$store ($count models)"
else
    info "$store (not created yet)"
fi

echo
if [ "${#missing[@]}" -eq 0 ]; then
    echo "ready to build"
    echo "  ./scripts/build-macos.sh        on macOS"
    echo "  cmake -S . -B build && cmake --build build -j    everywhere"
    exit 0
fi

echo "missing ${#missing[@]}:"
for item in "${missing[@]}"; do echo "  - $item"; done
exit 1
