#!/usr/bin/env bash
# Creates the development signing keypair once.
#
# The public half is committed (keys/dev.pub) and compiled into the app; the
# private half lives outside the repository and is used only by stemhand-sign.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$root/keys"

# A matching private key is what makes the public one usable. Without it the
# public key is a dead file from someone else's checkout, so regenerate both.
if [ -f "$root/keys/dev.pub" ] && [ -f "$root/.dev-keys/dev-signing.key" ]; then
    echo "signing key already present — nothing to do"
    exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/keygen.c" <<'EOF'
#include <sodium.h>
#include <stdio.h>
int main(void) {
    if (sodium_init() < 0) return 1;
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(pk, sk);
    for (int i = 0; i < crypto_sign_PUBLICKEYBYTES; ++i) printf("%02x", pk[i]);
    printf("\n");
    for (int i = 0; i < crypto_sign_SECRETKEYBYTES; ++i) printf("%02x", sk[i]);
    printf("\n");
    return 0;
}
EOF

# Ask pkg-config where libsodium actually is. On Apple Silicon a stale Intel
# Homebrew in /usr/local is still on the linker's default path, and a bare
# -lsodium picks up its x86_64 dylib instead of the arm64 one.
if command -v pkg-config > /dev/null && pkg-config --exists libsodium 2>/dev/null; then
    sodium_flags="$(pkg-config --cflags --libs libsodium)"
else
    sodium_flags="-lsodium"
fi
# shellcheck disable=SC2086
gcc -O2 -o "$tmp/keygen" "$tmp/keygen.c" $sodium_flags
"$tmp/keygen" > "$tmp/out"

head -n 1 "$tmp/out" > "$root/keys/dev.pub"

private_dir="$root/.dev-keys"
mkdir -p "$private_dir"
tail -n 1 "$tmp/out" > "$private_dir/dev-signing.key"
chmod 600 "$private_dir/dev-signing.key"

echo "public key  -> keys/dev.pub"
echo "private key -> $private_dir/dev-signing.key"
