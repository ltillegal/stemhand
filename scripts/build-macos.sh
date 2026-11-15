#!/usr/bin/env bash
# Builds Stemhand.app on Apple Silicon.
#
# Untested: there is no Mac in the development loop yet. The steps follow
# docs/MACOS.md, and every one of them is a plain command you can run by hand.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/build-macos"

if [ "$(uname -s)" != "Darwin" ]; then
    echo "this script builds the macOS app; run it on macOS" >&2
    exit 1
fi

for formula in qt onnxruntime libsodium ffmpeg pkg-config cmake; do
    if ! brew list --versions "$formula" > /dev/null 2>&1; then
        echo "missing: brew install $formula" >&2
        exit 1
    fi
done

qt_prefix="$(brew --prefix qt)"
ort_prefix="$(brew --prefix onnxruntime)"

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt_prefix" \
    -DONNXRUNTIME_ROOT="$ort_prefix"
cmake --build "$build" -j

app="$build/stemhand.app"
if [ ! -d "$app" ]; then
    echo "no app bundle at $app" >&2
    exit 1
fi

# Qt plugins and QML modules. -qmldir matters: our QML is compiled into the
# binary as resources, so macdeployqt has nothing on disk to scan and would
# otherwise ship no QML modules at all ("module QtQuick.Controls not found").
"$qt_prefix/bin/macdeployqt" "$app" -always-overwrite -qmldir="$root/qml"

mkdir -p "$app/Contents/Frameworks"
cp "$ort_prefix"/lib/libonnxruntime*.dylib "$app/Contents/Frameworks/"

binary="$app/Contents/MacOS/stemhand"
old_path="$(otool -L "$binary" | awk '/libonnxruntime/ { print $1; exit }')"
if [ -n "$old_path" ]; then
    install_name_tool -change "$old_path" "@rpath/$(basename "$old_path")" "$binary"
fi
install_name_tool -add_rpath "@executable_path/../Frameworks" "$binary" 2>/dev/null || true

# Ad-hoc signature: enough to run locally, not enough to distribute.
codesign --force --deep --sign - "$app"

echo
echo "built $app"
echo "try:  $binary --scan"
