#!/usr/bin/env bash
# Builds a file:// model registry for development and tests.
#
#   scripts/make-registry.sh [version] [registry-dir] [onnx]
#
# The registry is a plain directory: index.json plus one directory per model
# version holding manifest.json, manifest.json.sig and the weights. Pointing
# stemhand at it with file:// exercises exactly the same code path as https://.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-1.1.0}"
out="${2:-$root/build/registry}"
onnx="${3:-$root/models-dev/gtcrn/gtcrn_simple.onnx}"
key="$root/.dev-keys/dev-signing.key"
sign="$root/build/stemhand-sign"

[ -x "$sign" ] || { echo "stemhand-sign not built" >&2; exit 1; }
[ -f "$onnx" ] || { echo "no onnx at $onnx" >&2; exit 1; }

dir="$out/denoise/gtcrn/$version"
mkdir -p "$dir"
cp "$onnx" "$dir/gtcrn_simple.onnx"

sha="$(sha256sum "$dir/gtcrn_simple.onnx" | cut -d' ' -f1)"
size="$(stat -c%s "$dir/gtcrn_simple.onnx")"

python3 - "$dir/manifest.json" "$version" "$sha" "$size" <<'PY'
import json, sys
path, version, sha, size = sys.argv[1:]
manifest = {
    "schema": 1,
    "id": "denoise/gtcrn",
    "version": version,
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

python3 - "$out/index.json" <<'PY'
import json, sys
index = {
    "schema": 1,
    "models": [
        {
            "id": "denoise/gtcrn",
            "version": "1.1.0",
            "manifest": "denoise/gtcrn/1.1.0/manifest.json",
        }
    ],
}
open(sys.argv[1], "w").write(json.dumps(index, indent=2) + "\n")
PY

echo "$out/index.json"
