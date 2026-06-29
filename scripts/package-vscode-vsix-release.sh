#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

host_target() {
  local os_name arch_name
  os_name="$(uname -s | tr '[:upper:]' '[:lower:]')"
  arch_name="$(uname -m)"

  case "$os_name:$arch_name" in
    darwin:arm64) echo "darwin-arm64" ;;
    darwin:x86_64) echo "darwin-x64" ;;
    linux:aarch64|linux:arm64) echo "linux-arm64" ;;
    linux:x86_64|linux:amd64) echo "linux-x64" ;;
    *)
      echo "error: unsupported VS Code extension target for host ${os_name}-${arch_name}" >&2
      exit 1
      ;;
  esac
}

TARGET="${1:-$(host_target)}"
HOST_TARGET="$(host_target)"
if [[ "$TARGET" != "$HOST_TARGET" ]]; then
  echo "error: cross-target VSIX packaging is not supported by this script: target=$TARGET host=$HOST_TARGET" >&2
  exit 1
fi

case "$TARGET" in
  darwin-arm64|darwin-x64|linux-arm64|linux-x64) ;;
  *)
    echo "error: unsupported VS Code extension target: $TARGET" >&2
    exit 1
    ;;
esac

if [[ ! -f "build/release/build.ninja" ]]; then
  cmake --preset release
fi

cmake --build --preset build-release --target ahfl-lsp

RELEASE_LSP="build/release/src/tooling/lsp/ahfl-lsp"
if [[ ! -x "$RELEASE_LSP" ]]; then
  echo "error: release LSP binary not found or not executable: $RELEASE_LSP" >&2
  exit 1
fi

SERVER_DIR="tools/vscode/server"
SERVER_BINARY="$SERVER_DIR/ahfl-lsp"
rm -rf "$SERVER_DIR"
mkdir -p "$SERVER_DIR"
cp "$RELEASE_LSP" "$SERVER_BINARY"
chmod 755 "$SERVER_BINARY"

# Bundle the stdlib so the extension ships corelib sources (M3-1). The LSP
# resolves std::* via AHFL_STDLIB_SEARCH_ROOT; the extension launcher (TS) must
# point that env at the bundled <extension>/std path — that wiring is a
# tools/vscode/src follow-up, but the sources must be in the VSIX first.
STDLIB_DIR="tools/vscode/std"
rm -rf "$STDLIB_DIR"
mkdir -p "$STDLIB_DIR"
cp std/*.ahfl "$STDLIB_DIR"/

EXTENSION_VERSION="$(node -p "require('./tools/vscode/package.json').version")"
VSIX_NAME="ahfl-language-${EXTENSION_VERSION}-${TARGET}.vsix"

(
  cd tools/vscode
  if [[ ! -x node_modules/.bin/vsce ]]; then
    if ! command -v pnpm >/dev/null 2>&1; then
      corepack enable
      corepack prepare pnpm@10.10.0 --activate
    fi
    pnpm install --frozen-lockfile --ignore-scripts
    if [[ ! -x node_modules/.bin/vsce ]]; then
      echo "error: VS Code extension dependencies were not installed correctly" >&2
      exit 1
    fi
  fi
  mkdir -p dist
  pnpm run compile
  pnpm exec vsce package --no-dependencies --target "$TARGET" --out "dist/$VSIX_NAME"
)

echo "tools/vscode/dist/$VSIX_NAME"
