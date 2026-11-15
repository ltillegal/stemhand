#!/usr/bin/env bash
# Installs the GTCRN ONNX file as a signed model in a store.
#
#   scripts/install-gtcrn.sh <gtcrn_simple.onnx> [store-root] [signing-key]
#
# The manifest is generated from the actual file (sha256 + size), so it can
# never disagree with the weights it describes.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
onnx="${1:?usage: install-gtcrn.sh <gtcrn_simple.onnx> [store-root] [signing-key]}"
# Default to the store the app itself uses, so an install needs no path.
if [ "$(uname -s)" = "Darwin" ]; then
    default_store="$HOME/Library/Application Support/stemhand/models"
else
    default_store="${XDG_DATA_HOME:-$HOME/.local/share}/stemhand/models"
fi
store="${2:-$default_store}"
key="${3:-$root/.dev-keys/dev-signing.key}"
. "$root/scripts/lib/find-build.sh"
stemhand_locate_sign "$root" || exit 1
sign="$STEMHAND_SIGN_PATH"

[ -f "$onnx" ] || { echo "no such onnx: $onnx" >&2; exit 1; }
[ -f "$key" ] || { echo "no signing key at $key — run scripts/dev-keys.sh" >&2; exit 1; }

dir="$store/denoise/gtcrn/1.0.0"
mkdir -p "$dir"
cp "$onnx" "$dir/gtcrn_simple.onnx"

sha="$(sha256sum "$dir/gtcrn_simple.onnx" | cut -d' ' -f1)"
size="$(stat -c%s "$dir/gtcrn_simple.onnx")"

python3 - "$dir/manifest.json" "$sha" "$size" <<'PY'
import json, sys

path, sha, size = sys.argv[1:]
manifest = {
    "schema": 1,
    "id": "denoise/gtcrn",
    "version": "1.0.0",
    "name": "GTCRN",
    "kind": "denoise",
    "driver": "gtcrn",
    "engine_abi": 1,
    "sample_rate": 16000,
    "channels": 1,
    "stems": ["voice"],
    "eps": ["cpu", "coreml"],
    "min_engine": "0.1.0",
    "homepage": "https://github.com/Xiaobin-Rong/gtcrn",
    "license": {"code": "MIT", "weights": "MIT", "commercial": True},
    "files": [{"path": "gtcrn_simple.onnx", "sha256": sha, "size": int(size)}],
}
open(path, "w").write(json.dumps(manifest, indent=2) + "\n")
PY

"$sign" sign "$dir" "$key" > /dev/null
echo "$dir"
