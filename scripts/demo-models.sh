#!/usr/bin/env bash
# Builds a throwaway model store with signed, but fake, models. Used for UI
# work, screenshots and tests: no real weights, no network.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:-$root/build/demo-models}"
sign="${2:-$root/build/photone-sign}"
key="${3:-$root/.dev-keys/dev-signing.key}"

if [ ! -x "$sign" ]; then
    echo "photone-sign not built at $sign" >&2
    exit 1
fi
if [ ! -f "$key" ]; then
    echo "no signing key at $key — run scripts/dev-keys.sh" >&2
    exit 1
fi

rm -rf "$out"
mkdir -p "$out"

make_model() {
    local id="$1" version="$2" name="$3" kind="$4" stems="$5" license="$6" commercial="$7" kb="$8"
    local dir="$out/$id/$version"
    mkdir -p "$dir"
    head -c $((kb * 1024)) /dev/urandom > "$dir/weights.bin"

    local sha size
    sha="$(sha256sum "$dir/weights.bin" | cut -d' ' -f1)"
    size="$(stat -c%s "$dir/weights.bin")"

    python3 - "$dir/manifest.json" "$id" "$version" "$name" "$kind" "$stems" "$license" "$commercial" "$sha" "$size" <<'PY'
import json, sys
path, ident, version, name, kind, stems, license_name, commercial, sha, size = sys.argv[1:]
manifest = {
    "schema": 1,
    "id": ident,
    "version": version,
    "name": name,
    "kind": kind,
    "engine_abi": 1,
    "sample_rate": 48000,
    "channels": 2,
    "stems": stems.split(","),
    "eps": ["cpu", "coreml"],
    "min_engine": "0.1.0",
    "license": {
        "code": license_name,
        "weights": license_name,
        "commercial": commercial == "true",
    },
    "files": [{"path": "weights.bin", "sha256": sha, "size": int(size)}],
}
open(path, "w").write(json.dumps(manifest, indent=2) + "\n")
PY

    "$sign" sign "$dir" "$key" > /dev/null
}

make_model "denoise/deepfilternet3" "3.0.0" "DeepFilterNet 3" "denoise" "voice" "MIT" true 4200
make_model "music4/htdemucs" "4.0.0" "Demucs v4 htdemucs" "music4" "vocals,drums,bass,other" "MIT" true 12288
make_model "drums5/drumsep" "1.0.0" "DrumSep 5 stems" "drums5" "kick,snare,toms,hihat,cymbals" "unstated" false 6400

echo "$out"
