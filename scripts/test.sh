#!/usr/bin/env bash
# End-to-end checks for the model store. No display, no network, no real weights.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin="$root/build/photone"
sign="$root/build/photone-sign"
tmp="$root/build/test-models"

fail=0
check() {
    local label="$1" expected="$2" got="$3"
    if [ "$expected" = "$got" ]; then
        printf 'ok   %s\n' "$label"
    else
        printf 'FAIL %s (expected %s, got %s)\n' "$label" "$expected" "$got"
        fail=1
    fi
}

"$root/scripts/demo-models.sh" > /dev/null

check "scan exits 0 on a healthy store" 0 \
    "$("$bin" --scan --root "$root/build/demo-models" > /dev/null 2>&1; echo $?)"

check "three models verified" 3 \
    "$("$bin" --scan --json --root "$root/build/demo-models" | grep -c '"status": "ok"')"

rm -rf "$tmp"
cp -r "$root/build/demo-models" "$tmp"
dd if=/dev/urandom of="$tmp/music4/htdemucs/4.0.0/weights.bin" bs=1 seek=64 conv=notrunc count=1 status=none

check "tampered weights exit 1" 1 \
    "$("$bin" --scan --root "$tmp" > /dev/null 2>&1; echo $?)"

check "tampered weights reported corrupt" 1 \
    "$("$bin" --scan --json --root "$tmp" | grep -c '"status": "corrupt"')"

rm "$tmp/drums5/drumsep/1.0.0/manifest.json.sig"

check "unsigned model reported unverified" 1 \
    "$("$bin" --scan --json --root "$tmp" | grep -c '"status": "unverified"')"

check "sign tool accepts a good signature" 0 \
    "$("$sign" verify "$root/build/demo-models/denoise/deepfilternet3/3.0.0" "$root/keys/dev.pub" > /dev/null 2>&1; echo $?)"

check "sign tool rejects a modified manifest" 1 \
    "$(cp "$root/build/demo-models/denoise/deepfilternet3/3.0.0/manifest.json" "$tmp/m.json"
       echo ' ' >> "$tmp/m.json"
       cp "$tmp/m.json" "$tmp/denoise/deepfilternet3/3.0.0/manifest.json"
       "$sign" verify "$tmp/denoise/deepfilternet3/3.0.0" "$root/keys/dev.pub" > /dev/null 2>&1; echo $?)"

# Engine checks, only when the dev model and its weights are actually present.
input="$root/models-dev/gtcrn/mix.wav"
voice="$root/build/models/denoise/gtcrn/1.0.0/gtcrn_simple.onnx"
if [ -f "$input" ] && [ -f "$voice" ]; then
    outdir="$root/build/test-denoise"
    mkdir -p "$outdir"
    "$bin" --denoise "$input" "$outdir/a.wav" --root "$root/build/models" > /dev/null 2>&1
    "$bin" --denoise "$input" "$outdir/b.wav" --root "$root/build/models" > /dev/null 2>&1

    check "denoise is deterministic" \
        "$(sha256sum "$outdir/a.wav" | cut -d' ' -f1)" \
        "$(sha256sum "$outdir/b.wav" | cut -d' ' -f1)"

    check "denoise changed the signal" \
        "different" \
        "$([ "$(sha256sum "$outdir/a.wav" | cut -d' ' -f1)" != "$(sha256sum "$input" | cut -d' ' -f1)" ] && echo different || echo same)"

    check "denoise writes a real wav" ">200000 bytes" \
        "$([ "$(stat -c%s "$outdir/a.wav")" -gt 200000 ] && echo '>200000 bytes' || echo too-small)"
fi

# Registry checks against the local file:// fixture from make-registry.sh.
registry="$root/build/registry/index.json"
if [ -f "$registry" ]; then
    fresh="$root/build/test-update"
    rm -rf "$fresh"
    mkdir -p "$fresh"

    "$bin" --update "file://$registry" --root "$fresh" > /dev/null 2>&1
    check "update installs a verified model" 1 \
        "$("$bin" --scan --json --root "$fresh" | grep -c '"status": "ok"')"

    check "update is idempotent" "up to date" \
        "$("$bin" --update "file://$registry" --root "$fresh" 2>&1 | grep -o 'up to date' | head -1)"

    # Upgrade path: keep 1.0.0, install 1.1.0 next to it, newest first.
    upgrade="$root/build/test-upgrade"
    rm -rf "$upgrade"
    cp -r "$root/build/models" "$upgrade"
    "$bin" --update "file://$registry" --root "$upgrade" > /dev/null 2>&1
    check "upgrade keeps the old version" 2 \
        "$("$bin" --scan --json --root "$upgrade" | grep -c '"id"')"
    check "newest version is listed first" "1.1.0" \
        "$("$bin" --scan --json --root "$upgrade" | grep '"version"' | head -1 | cut -d'"' -f4)"

    bad="$root/build/registry-bad"
    rm -rf "$bad"
    cp -r "$root/build/registry" "$bad"
    printf 'X' | dd of="$bad/denoise/gtcrn/1.1.0/gtcrn_simple.onnx" bs=1 seek=5000 conv=notrunc status=none

    poisoned="$root/build/test-update-bad"
    rm -rf "$poisoned"
    mkdir -p "$poisoned"
    status=0
    "$bin" --update "file://$bad/index.json" --root "$poisoned" > /dev/null 2>&1 || status=$?
    check "tampered weights are refused" 1 "$status"
    check "a failed update leaves no model behind" 0 \
        "$("$bin" --scan --json --root "$poisoned" | grep -c '"id"')"
fi

rm -rf "$tmp"

if [ "$fail" -ne 0 ]; then
    echo "FAILED"
    exit 1
fi
echo "all checks passed"
