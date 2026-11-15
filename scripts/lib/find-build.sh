#!/usr/bin/env bash
# Locates the build artefacts. Sourced by the scripts that need the binary or
# the sign tool.
#
# The build directory is not fixed: `build/` on Linux, `build-macos/` from
# scripts/build-macos.sh, and on macOS the binary lives inside the .app bundle
# while stemhand-sign sits next to the bundle. Every script that used to guess
# "$root/build/..." now asks here instead.
#
# Usage:
#   . "$root/scripts/lib/find-build.sh"
#   stemhand_locate "$root" || exit 1
#   # -> STEMHAND_BIN_PATH, STEMHAND_SIGN_PATH
# shellcheck shell=bash

stemhand_locate() {
    local root="$1"
    local bin="${STEMHAND_BIN:-}"

    if [ -z "$bin" ]; then
        local candidate
        for candidate in \
            "$root/build/stemhand" \
            "$root/build-macos/stemhand" \
            "$root/build-macos/stemhand.app/Contents/MacOS/stemhand" \
            "$root/build/stemhand.app/Contents/MacOS/stemhand"; do
            if [ -x "$candidate" ]; then
                bin="$candidate"
                break
            fi
        done
    fi

    if [ -z "$bin" ] || [ ! -x "$bin" ]; then
        echo "no built binary: run cmake --build first, or set STEMHAND_BIN" >&2
        return 1
    fi

    local build_dir
    case "$bin" in
        # "…/build-macos/stemhand.app/Contents/MacOS/stemhand" -> "…/build-macos"
        *.app/Contents/MacOS/*) build_dir="$(dirname "${bin%%.app/*}")" ;;
        *) build_dir="$(dirname "$bin")" ;;
    esac

    STEMHAND_BIN_PATH="$bin"
    STEMHAND_SIGN_PATH="$build_dir/stemhand-sign"
    export STEMHAND_BIN_PATH STEMHAND_SIGN_PATH
}

stemhand_locate_sign() {
    local root="$1"
    local sign="${STEMHAND_SIGN:-}"
    if [ -z "$sign" ] && [ -n "${STEMHAND_SIGN_PATH:-}" ]; then
        sign="$STEMHAND_SIGN_PATH"
    fi
    if [ -z "$sign" ]; then
        # Ask the locator; a missing binary is fine here, a missing sign tool is not.
        stemhand_locate "$root" > /dev/null 2>&1 || true
        sign="${STEMHAND_SIGN_PATH:-}"
    fi
    if [ -z "$sign" ] || [ ! -x "$sign" ]; then
        echo "stemhand-sign not found: build it with cmake --build, or set STEMHAND_SIGN" >&2
        return 1
    fi
    STEMHAND_SIGN_PATH="$sign"
    export STEMHAND_SIGN_PATH
}
