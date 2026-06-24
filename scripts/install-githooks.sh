#!/bin/sh
# scripts/install-githooks.sh
#
# Install (or refresh) AHFL's versioned git hooks into your local clone.
#
# Strategy: instead of overriding core.hooksPath (which would clobber any
# hooks you have configured globally), we install individual hooks as
# symlinks inside the default .git/hooks/ directory. That way:
#   * only the hooks we ship are replaced,
#   * the VibeBuddy post-commit hook (or any other tool's hooks) keeps
#     working,
#   * edits to scripts/githooks/* take effect immediately on the next
#     invocation without re-running this script.
#
# If you DO want to use core.hooksPath instead (e.g. on Windows without
# symlinks), re-run:
#     scripts/install-githooks.sh --via-hooksPath
# …and git will source every hook from scripts/githooks/ directly. This
# second mode means any third-party hook managed by other tools (e.g. the
# VibeBuddy post-commit) must be manually reconciled, so we default to the
# per-hook symlink approach.

set -eu

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$REPO_ROOT/scripts/githooks"
HOOKS_DIR="$REPO_ROOT/.git/hooks"

MODE=symlink
if [ "${1:-}" = "--via-hooksPath" ]; then
    MODE=hooksPath
fi

mkdir -p "$HOOKS_DIR"

install_one() {
    name="$1"
    src="$SRC_DIR/$name"
    dst="$HOOKS_DIR/$name"

    if [ ! -f "$src" ]; then
        echo "  skip  $name  (not present in scripts/githooks/)"
        return 0
    fi
    chmod +x "$src"

    # Already a correct symlink → nothing to do.
    if [ -L "$dst" ] && [ "$(readlink "$dst" 2>/dev/null || true)" = "$src" ]; then
        echo "  ok    $name  (already linked)"
        return 0
    fi

    # Already exists and is NOT one of ours → back it up.
    if [ -e "$dst" ] && [ ! -L "$dst" ]; then
        bak="$dst.bak.$(date +%Y%m%d-%H%M%S)"
        echo "  save  $name  → $bak (pre-existing non-AHFL hook preserved)"
        mv "$dst" "$bak"
    elif [ -L "$dst" ]; then
        # Stale / wrong symlink → remove and re-link.
        rm -f "$dst"
    fi

    ln -s "$src" "$dst"
    echo "  link  $name  → scripts/githooks/$name"
}

echo "AHFL git hooks installer — mode: $MODE"
echo "repo:  $REPO_ROOT"
echo ""

if [ "$MODE" = "symlink" ]; then
    # Find every file directly under scripts/githooks/ and install by name.
    find "$SRC_DIR" -type f -maxdepth 1 -mindepth 1 -print | \
        sort | while read -r src; do
            install_one "$(basename "$src")"
        done
    echo ""
    echo "Done. If you add new hooks to scripts/githooks/, re-run this script."
else
    # --via-hooksPath: point git at the directory directly.
    ABS_SRC="$(cd "$SRC_DIR" && pwd)"
    git -C "$REPO_ROOT" config core.hooksPath "$ABS_SRC"
    echo "  set   core.hooksPath = $ABS_SRC"
    echo ""
    echo "Done. NOTE: with core.hooksPath, git ignores every hook that does"
    echo "not exist under scripts/githooks/. Third-party hooks (e.g. the"
    echo "VibeBuddy post-commit) must be copied into that directory manually."
fi
