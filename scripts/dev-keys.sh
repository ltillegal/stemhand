#!/usr/bin/env bash
# Creates the development signing keypair once.
#
# The public half is committed (keys/dev.pub) and compiled into the app; the
# private half lives outside the repository and is used only by photone-sign.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$root/keys"

if [ -f "$root/keys/dev.pub" ]; then
    echo "keys/dev.pub already exists — nothing to do"
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

gcc -O2 -o "$tmp/keygen" "$tmp/keygen.c" -lsodium
"$tmp/keygen" > "$tmp/out"

head -n 1 "$tmp/out" > "$root/keys/dev.pub"

private_dir="$root/.dev-keys"
mkdir -p "$private_dir"
tail -n 1 "$tmp/out" > "$private_dir/dev-signing.key"
chmod 600 "$private_dir/dev-signing.key"

echo "public key  -> keys/dev.pub"
echo "private key -> $private_dir/dev-signing.key"
