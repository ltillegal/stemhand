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

rm "$tmp/drums5/drumsep/1.0.0/manifest.sig"

check "unsigned model reported unverified" 1 \
    "$("$bin" --scan --json --root "$tmp" | grep -c '"status": "unverified"')"

check "sign tool accepts a good signature" 0 \
    "$("$sign" verify "$root/build/demo-models/denoise/deepfilternet3/3.0.0" "$root/keys/dev.pub" > /dev/null 2>&1; echo $?)"

check "sign tool rejects a modified manifest" 1 \
    "$(cp "$root/build/demo-models/denoise/deepfilternet3/3.0.0/manifest.json" "$tmp/m.json"
       echo ' ' >> "$tmp/m.json"
       cp "$tmp/m.json" "$tmp/denoise/deepfilternet3/3.0.0/manifest.json"
       "$sign" verify "$tmp/denoise/deepfilternet3/3.0.0" "$root/keys/dev.pub" > /dev/null 2>&1; echo $?)"

rm -rf "$tmp"

if [ "$fail" -ne 0 ]; then
    echo "FAILED"
    exit 1
fi
echo "all checks passed"
