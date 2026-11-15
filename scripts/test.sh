#!/usr/bin/env bash
# End-to-end checks for the model store. No display, no network, no real weights.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Locate the build artefacts (build/ or build-macos/, plain or inside .app).
. "$root/scripts/lib/find-build.sh"
stemhand_locate "$root" || exit 1
bin="$STEMHAND_BIN_PATH"
sign="$STEMHAND_SIGN_PATH"

fail=0
tmp="$root/build/test-models"

# Mean volume in dB as reported by ffmpeg's volumedetect; empty if unreadable.
mean_volume() {
    ffmpeg -hide_banner -nostats -i "$1" -af volumedetect -f null - 2>&1 \
        | sed -n 's/.*mean_volume: \([-0-9.]*\) dB.*/\1/p' | tail -1
}
check() {
    local label="$1" expected="$2" got="$3"
    if [ "$expected" = "$got" ]; then
        printf 'ok   %s\n' "$label"
    else
        printf 'FAIL %s (expected %s, got %s)\n' "$label" "$expected" "$got"
        fail=1
    fi
}

"$root/scripts/demo-models.sh" "$root/build/demo-models" "$sign" "$root/.dev-keys/dev-signing.key" > /dev/null

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

# Four-stem separation: only when the 300 MB model is actually installed.
demucs="$root/build/models/music4/htdemucs/4.0.0/htdemucs.onnx"
if [ -f "$demucs" ] && [ -f "$root/build/testmix/mix.wav" ]; then
    short="$root/build/testmix/mix3.wav"
    ffmpeg -v error -y -i "$root/build/testmix/mix.wav" -t 3 "$short"

    stems="$root/build/test-separate"
    rm -rf "$stems"
    "$bin" --separate "$short" "$stems" --root "$root/build/models" > /dev/null 2>&1

    check "separate writes four stems" 4 "$(ls "$stems"/*.wav 2>/dev/null | wc -l)"

    quiet=0
    for f in "$stems"/*.wav; do
        level="$(mean_volume "$f")"
        # -91 dB is digital silence; anything above -60 dB carries signal.
        if [ -z "$level" ] || [ "$(awk -v v="$level" 'BEGIN { print (v < -60) ? 1 : 0 }')" -eq 1 ]; then
            quiet=$((quiet + 1))
        fi
    done
    check "stems carry signal" 0 "$quiet"

    "$bin" --separate "$short" "$root/build/test-separate-b" --root "$root/build/models" > /dev/null 2>&1
    check "separation is deterministic" \
        "$(sha256sum "$stems/mix3_vocals.wav" | cut -d' ' -f1)" \
        "$(sha256sum "$root/build/test-separate-b/mix3_vocals.wav" | cut -d' ' -f1)"
fi

# Mixer: solo must isolate a stem sample-for-sample, mute-all must be silence.
stemsdir="$root/build/testmix/mix3_stems"
if [ -d "$stemsdir" ]; then
    "$bin" --mix "$stemsdir" "$root/build/test-mix-solo.wav" --solo vocals > /dev/null 2>&1
    "$bin" --mix "$stemsdir" "$root/build/test-mix-mute.wav" \
        --mute drums --mute bass --mute other --mute vocals > /dev/null 2>&1

    check "solo isolates the stem" \
        "$(sha256sum "$stemsdir/mix3_vocals.wav" | cut -d' ' -f1)" \
        "$(sha256sum "$root/build/test-mix-solo.wav" | cut -d' ' -f1)"

    check "muting everything is silenced" "-91.0" \
        "$(mean_volume "$root/build/test-mix-mute.wav")"
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
    # Copy only the model under test: copying the whole store duplicates the
    # 290 MB htdemucs file on every run for no reason.
    upgrade="$root/build/test-upgrade"
    rm -rf "$upgrade"
    mkdir -p "$upgrade/denoise"
    cp -r "$root/build/models/denoise/gtcrn" "$upgrade/denoise/"
    "$bin" --update "file://$registry" --root "$upgrade" > /dev/null 2>&1
    check "upgrade keeps the old version" 2 \
        "$("$bin" --scan --json --root "$upgrade" | grep -c '"id": "denoise/gtcrn"')"
    check "newest version is listed first" "1.1.0" \
        "$("$bin" --scan --json --root "$upgrade" | grep '"version"' | head -1 | cut -d'"' -f4)"

    # Pinning: an update must not silently change what a project runs on.
    "$bin" --prefer "denoise/gtcrn@1.0.0" --root "$upgrade" > /dev/null 2>&1
    check "pin wins over newest" "1.0.0" \
        "$("$bin" --scan --json --root "$upgrade" | grep '"version"' | head -1 | cut -d'"' -f4)"
    check "denoise honours the pin" "1.0.0" \
        "$("$bin" --denoise "$root/models-dev/gtcrn/mix.wav" "$upgrade/pinned.wav" --root "$upgrade" 2>/dev/null \
           | grep -o 'gtcrn [0-9.]*' | awk '{print $2}')"
    "$bin" --release denoise/gtcrn --root "$upgrade" > /dev/null 2>&1
    check "release falls back to newest" "1.1.0" \
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

    rm -rf "$upgrade" "$fresh" "$poisoned" "$bad" "$root/build/registry-bad"
fi

rm -rf "$tmp"

if [ "$fail" -ne 0 ]; then
    echo "FAILED"
    exit 1
fi
echo "all checks passed"
