#!/usr/bin/env bash
# Portable stand-ins for tools that differ between GNU and BSD userlands.
#
# macOS ships BSD stat (no -c) and no sha256sum; it has shasum instead. Linux
# has the opposite. Sourcing this file gives every script one spelling.
# shellcheck shell=bash

# Lowercase hex SHA-256 of a file, or empty on failure.
sha256_of() {
    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum "$1" 2>/dev/null | awk '{ print $1; exit }'
    elif command -v shasum > /dev/null 2>&1; then
        shasum -a 256 "$1" 2>/dev/null | awk '{ print $1; exit }'
    else
        openssl dgst -sha256 "$1" 2>/dev/null | awk '{ print $NF; exit }'
    fi
}

# Size of a file in bytes, without GNU stat's -c flag.
file_size() {
    wc -c < "$1" | tr -d '[:space:]'
}
