#!/usr/bin/env bash
# scripts/bootstrap-wasmtime.sh
#
# Explicit, opt-in installer for a pinned, checksum-verified `wasmtime` used by
# the optional RFC 0026 Objective 6A WASM tests (KR6.4-pre preflight and, later,
# the Core-IR -> WASM execution suite).
#
# This is a supply-chain boundary (it downloads and installs an executable), so:
#   * only PINNED versions with a KNOWN SHA-256 are installed by default;
#   * a custom --version REQUIRES an accompanying --sha256 (of the .tar.xz
#     asset) — we refuse to install an unverified download;
#   * the pinned version is enforced to be >= the harness minimum (kept in sync
#     with tests/scripts/wasm_preflight.py MIN_WASMTIME).
#
# The default `cmake --preset dev` / `ctest` flow performs NO network access and
# does NOT run this script. wasmtime is a TEST/HOST tool, never linked into the
# AHFL core build (RFC 0019/0021 no-strong-runtime boundary). You run this by
# hand, once, to turn the real-execution WASM tests from SKIP into RUN.
set -euo pipefail

# --- pins (keep MIN_MAJOR in sync with wasm_preflight.py MIN_WASMTIME major) ---
DEFAULT_VERSION="27.0.0"
MIN_MAJOR=15   # matches MIN_WASMTIME = (15, 0, 0)

# Known-good SHA-256 of each pinned release asset, keyed "version:triple".
# FAIL-CLOSED: entries are intentionally EMPTY until a maintainer with network
# access populates the real hashes from the wasmtime release's published
# `<asset>.sha256` files (https://github.com/bytecodealliance/wasmtime/releases).
# While empty, EVERY install — including the default version — requires an
# explicit, caller-supplied `--sha256`, so we never install an unverified
# download and never ship a fabricated checksum. Fill these in (and only these)
# when bumping DEFAULT_VERSION.
sha256_for_pin() {
    case "$1" in
        "27.0.0:x86_64-linux")  echo "" ;;
        "27.0.0:aarch64-linux") echo "" ;;
        "27.0.0:x86_64-macos")  echo "" ;;
        "27.0.0:aarch64-macos") echo "" ;;
        *) echo "" ;;
    esac
}

usage() {
    cat <<'USAGE'
bootstrap-wasmtime.sh — install a pinned, checksum-verified wasmtime for the
optional RFC 0026 WASM tests.

USAGE:
  scripts/bootstrap-wasmtime.sh [--prefix DIR] [--version X.Y.Z --sha256 HEX]

OPTIONS:
  --prefix DIR     install prefix (default: ./.wasmtime; git-ignored)
  --version X.Y.Z  install a specific version instead of the pinned default;
                   REQUIRES --sha256 (of the .tar.xz asset) unless the version
                   is already in this script's known-good table
  --sha256 HEX     expected SHA-256 of the downloaded .tar.xz asset
  -h, --help       show this help

After install, enable the tests by configuring with:
  cmake --preset dev -DAHFL_WASMTIME=<prefix>/wasmtime
  ctest --preset test-dev -L wasm --output-on-failure
USAGE
}

PREFIX="./.wasmtime"
VERSION="${WASMTIME_VERSION:-${DEFAULT_VERSION}}"
EXPECT_SHA="${WASMTIME_SHA256:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)  PREFIX="$2"; shift 2 ;;
        --prefix=*) PREFIX="${1#--prefix=}"; shift ;;
        --version) VERSION="$2"; shift 2 ;;
        --version=*) VERSION="${1#--version=}"; shift ;;
        --sha256)  EXPECT_SHA="$2"; shift 2 ;;
        --sha256=*) EXPECT_SHA="${1#--sha256=}"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# Enforce the minimum major version the harness accepts.
major="${VERSION%%.*}"
if ! printf '%s' "${major}" | grep -Eq '^[0-9]+$'; then
    echo "invalid --version '${VERSION}' (expected X.Y.Z)" >&2
    exit 2
fi
if [ "${major}" -lt "${MIN_MAJOR}" ]; then
    echo "refusing to install wasmtime ${VERSION}: below minimum major ${MIN_MAJOR}" >&2
    echo "(the preflight requires >= ${MIN_MAJOR}.0.0; see wasm_preflight.py)" >&2
    exit 2
fi

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

# Resolve the expected checksum: caller-supplied wins; else the pinned table.
if [ -z "${EXPECT_SHA}" ]; then
    EXPECT_SHA="$(sha256_for_pin "${VERSION}:${triple}")"
fi
if [ -z "${EXPECT_SHA}" ]; then
    echo "no known SHA-256 for wasmtime ${VERSION} (${triple})." >&2
    echo "refusing to install an unverified download — pass --sha256 <hex> of the" >&2
    echo ".tar.xz asset, or use the pinned default version ${DEFAULT_VERSION}." >&2
    exit 2
fi

asset="wasmtime-v${VERSION}-${triple}.tar.xz"
url="https://github.com/bytecodealliance/wasmtime/releases/download/v${VERSION}/${asset}"

mkdir -p "${PREFIX}"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

echo "Downloading wasmtime v${VERSION} (${triple})..."
if command -v curl >/dev/null 2>&1; then
    curl -fSL "${url}" -o "${tmp}/${asset}"
elif command -v wget >/dev/null 2>&1; then
    wget -O "${tmp}/${asset}" "${url}"
else
    echo "need curl or wget to download wasmtime" >&2
    exit 1
fi

# Verify the archive checksum BEFORE extracting or installing anything.
if command -v sha256sum >/dev/null 2>&1; then
    actual="$(sha256sum "${tmp}/${asset}" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
    actual="$(shasum -a 256 "${tmp}/${asset}" | awk '{print $1}')"
else
    echo "need sha256sum or shasum to verify the download" >&2
    exit 1
fi
if [ "${actual}" != "${EXPECT_SHA}" ]; then
    echo "SHA-256 MISMATCH for ${asset}" >&2
    echo "  expected: ${EXPECT_SHA}" >&2
    echo "  actual:   ${actual}" >&2
    echo "refusing to install a tampered/incorrect download." >&2
    exit 1
fi
echo "Checksum verified: ${actual}"

tar -xf "${tmp}/${asset}" -C "${tmp}"
bin="${tmp}/wasmtime-v${VERSION}-${triple}/wasmtime"
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
