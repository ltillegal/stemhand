#!/usr/bin/env bash
# Installs htdemucs.onnx as a signed model in a store.
#
#   scripts/install-demucs.sh <htdemucs.onnx> [store-root] [signing-key]
#
# Note on licensing: Meta publishes the Demucs weights as MIT, the ONNX mirror
# this file comes from tags them CC-BY-NC-4.0. For a personal install either
# reading is fine; the manifest records both so a redistribution decision is
# not made silently.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$root/scripts/lib/portable.sh"
onnx="${1:?usage: install-demucs.sh <htdemucs.onnx> [store-root] [signing-key]}"
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

dir="$store/music4/htdemucs/4.0.0"
mkdir -p "$dir"
cp "$onnx" "$dir/htdemucs.onnx"

sha="$(sha256_of "$dir/htdemucs.onnx")"
size="$(file_size "$dir/htdemucs.onnx")"

python3 - "$dir/manifest.json" "$sha" "$size" <<'PY'
import json, sys

path, sha, size = sys.argv[1:]
manifest = {
    "schema": 1,
    "id": "music4/htdemucs",
    "version": "4.0.0",
    "name": "Demucs v4 htdemucs",
    "kind": "music4",
    "driver": "htdemucs",
    "engine_abi": 1,
    "sample_rate": 44100,
    "channels": 2,
    "stems": ["drums", "bass", "other", "vocals"],
    "eps": ["cpu", "coreml"],
    "min_engine": "0.1.0",
    "homepage": "https://github.com/facebookresearch/demucs",
    "license": {
        "code": "MIT",
        "weights": "MIT (Meta release) / cc-by-nc-4.0 (ONNX mirror)",
        "commercial": False,
    },
    "files": [{"path": "htdemucs.onnx", "sha256": sha, "size": int(size)}],
}
open(path, "w").write(json.dumps(manifest, indent=2) + "\n")
PY

"$sign" sign "$dir" "$key" > /dev/null
echo "$dir"
