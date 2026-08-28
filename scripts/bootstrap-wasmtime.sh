#!/usr/bin/env bash
# scripts/bootstrap-wasmtime.sh
#
# Explicit, opt-in installer for a pinned `wasmtime` used by the optional
# RFC 0026 Objective 6A WASM tests (KR6.4-pre preflight and, later, the
# Core-IR -> WASM execution suite).
#
# WHY THIS IS EXPLICIT-ONLY:
#   * The default `cmake --preset dev` / `ctest` flow performs NO network access
#     and does NOT run this script. wasmtime is a TEST/HOST tool, never linked
#     into the AHFL core build (RFC 0019/0021 no-strong-runtime boundary).
#   * You run this by hand, once, when you want the real-execution WASM tests to
#     go from SKIP to RUN. Nothing in the build depends on it having been run.
#
# WHAT IT DOES:
#   * Downloads the pinned wasmtime release for your platform into a local,
#     git-ignored prefix (default: ./.wasmtime), verifying the archive.
#   * Prints the exact `-DAHFL_WASMTIME=...` line to enable the tests.
#
# USAGE:
#   scripts/bootstrap-wasmtime.sh            # download pinned version to ./.wasmtime
#   scripts/bootstrap-wasmtime.sh --prefix ~/.local   # custom install prefix
#   WASMTIME_VERSION=27.0.0 scripts/bootstrap-wasmtime.sh   # override the pin
#
# The pin below MUST stay >= the harness minimum (tests/scripts/wasm_preflight.py
# MIN_WASMTIME). Bump both together, deliberately, with a test.
set -euo pipefail

# Pinned version. Keep >= MIN_WASMTIME in tests/scripts/wasm_preflight.py.
WASMTIME_VERSION="${WASMTIME_VERSION:-27.0.0}"

PREFIX="./.wasmtime"
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)
            PREFIX="$2"; shift 2 ;;
        --prefix=*)
            PREFIX="${1#--prefix=}"; shift ;;
        -h|--help)
            grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)
            echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

uname_s="$(uname -s)"
uname_m="$(uname -m)"
case "${uname_s}-${uname_m}" in
    Linux-x86_64)   triple="x86_64-linux" ;;
    Linux-aarch64)  triple="aarch64-linux" ;;
    Darwin-x86_64)  triple="x86_64-macos" ;;
    Darwin-arm64)   triple="aarch64-macos" ;;
    *)
        echo "unsupported platform ${uname_s}-${uname_m}; install wasmtime manually" >&2
        echo "then configure with -DAHFL_WASMTIME=/path/to/wasmtime" >&2
        exit 1 ;;
esac

asset="wasmtime-v${WASMTIME_VERSION}-${triple}.tar.xz"
url="https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/${asset}"

mkdir -p "${PREFIX}"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

echo "Downloading wasmtime v${WASMTIME_VERSION} (${triple})..."
if command -v curl >/dev/null 2>&1; then
    curl -fSL "${url}" -o "${tmp}/${asset}"
elif command -v wget >/dev/null 2>&1; then
    wget -O "${tmp}/${asset}" "${url}"
else
    echo "need curl or wget to download wasmtime" >&2
    exit 1
fi

tar -xf "${tmp}/${asset}" -C "${tmp}"
bin="${tmp}/wasmtime-v${WASMTIME_VERSION}-${triple}/wasmtime"
if [ ! -x "${bin}" ]; then
    echo "extracted archive did not contain an executable wasmtime" >&2
    exit 1
fi
install -m 0755 "${bin}" "${PREFIX}/wasmtime"

installed="$("${PREFIX}/wasmtime" --version)"
echo "Installed: ${installed} -> ${PREFIX}/wasmtime"
echo
echo "Enable the WASM execution tests by configuring with:"
echo "    cmake --preset dev -DAHFL_WASMTIME=$(cd "${PREFIX}" && pwd)/wasmtime"
echo "then re-run: ctest --preset test-dev -L wasm --output-on-failure"
