#!/usr/bin/env bash
# Builds a portable Linux tarball: the app, the sign tool, the ONNX Runtime
# shared library it links against, and a launcher that points at it.
#
#   scripts/package-linux.sh [output-dir]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$root/scripts/lib/find-build.sh"
stemhand_locate "$root"
bin="$STEMHAND_BIN_PATH"
sign="$STEMHAND_SIGN_PATH"

version="$("$bin" --version | awk '{print $2}')"
arch="$(uname -m)"
out="${1:-$root/build}"
stage="$out/stemhand-$version-linux-$arch"

rm -rf "$stage"
mkdir -p "$stage/bin" "$stage/lib"

cp "$bin" "$stage/bin/stemhand"
cp "$sign" "$stage/bin/stemhand-sign"

# The shared library the binary was linked against, with its symlinks.
ort_lib="$(ldd "$bin" | awk '/libonnxruntime/ { print $3; exit }')"
if [ -n "$ort_lib" ] && [ -f "$ort_lib" ]; then
    cp -a "$(dirname "$ort_lib")/libonnxruntime.so"* "$stage/lib/"
else
    echo "warning: libonnxruntime not found next to the binary; the tarball will need it" >&2
fi

cat > "$stage/run.sh" <<'RUN'
#!/usr/bin/env bash
# Runs the bundled build without touching the system library paths.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$here/bin/stemhand" "$@"
RUN
chmod +x "$stage/run.sh"

cp "$root/README.md" "$root/LICENSE" "$stage/"

tarball="$out/stemhand-$version-linux-$arch.tar.gz"
tar -czf "$tarball" -C "$out" "$(basename "$stage")"
echo "$tarball"
