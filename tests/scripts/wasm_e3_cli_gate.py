#!/usr/bin/env python3
"""Always-on E3 package-entry and direct-producer byte identity gate."""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        return fail(
            "usage: wasm_e3_cli_gate.py <ahflc> <producer> <source> <manifest>"
        )
    ahflc, producer, source, manifest = map(Path, argv)
    if not all(path.is_file() for path in (ahflc, producer, source, manifest)):
        return fail("missing E3 CLI gate input")

    with tempfile.TemporaryDirectory(prefix="ahfl-e3-cli-") as td:
        direct_path = Path(td) / "direct.wasm"
        direct = subprocess.run(
            [str(producer), str(source), str(direct_path)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if direct.returncode != 0:
            return fail(
                f"E3 direct producer exited {direct.returncode}: {direct.stderr!r}"
            )

        cli = subprocess.run(
            [
                str(ahflc),
                "emit",
                "wasm",
                "--manifest",
                str(manifest),
                "--target",
                "workflow",
            ],
            capture_output=True,
            timeout=60,
        )
        if cli.returncode != 0:
            return fail(
                f"E3 package CLI exited {cli.returncode}: "
                f"{cli.stderr.decode(errors='replace')!r}"
            )
        direct_bytes = direct_path.read_bytes()
        if cli.stdout != direct_bytes:
            return fail("typed package entry and direct Core workflow target emitted different bytes")
        if not cli.stdout.startswith(b"\x00asm\x01\x00\x00\x00"):
            return fail("E3 CLI output is not a wasm v1 binary")

    print("E3 package CLI typed-entry byte identity passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
