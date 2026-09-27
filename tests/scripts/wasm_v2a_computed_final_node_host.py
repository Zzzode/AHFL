#!/usr/bin/env python3
"""RFC 0026 P6-7 frame-bridge v2 rung V2-A Node execution evidence.

Real Node v22 (NOT wasmtime) evidence for COMPUTED FINALS:
the emitted module is a p6-frame agent whose final region
constructs its output (a scalar, a nested aggregate, or a tag
enum through an if). This host:

  1. parses the producer's `computed_final=1 ...` frame report;
  2. packs the input frame as raw words at the fixed input base;
  3. drives runv and asserts (OK, output_base=12288);
  4. walks the fixed output words and compares them against the
     expected values, including zero padding.

The probe never emits a machine-readable descriptor (it is a node-only
fixture, NOT a census case), so this script is a self-contained P4-D
frame walker mirroring the producer's reported leaf offsets -- no
JSON encode/decode, just the word-level frame contract.

SKIP (77) when the node interpreter is unavailable.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def parse_report(stdout: str) -> dict[str, object]:
    match = re.search(r"computed_final=1 input_base=(\d+) output_base=(\d+) "
                    r"input_size=(\d+) output_size=(\d+) inputs=(\S*) "
                    r"outputs=(\S*)", stdout)
    if match is None:
        raise ValueError(f"no computed_final frame report in: {stdout!r}")
    def leaves(blob: str) -> list[tuple[int, int]]:
        if not blob:
            return []
        out = []
        for entry in blob.split(","):
            off, width = entry[1:].split(":")
            out.append((int(off), int(width)))
        return out
    return {
        "input_base": int(match.group(1)),
        "output_base": int(match.group(2)),
        "input_size": int(match.group(3)),
        "output_size": int(match.group(4)),
        "inputs": leaves(match.group(5)),
        "outputs": leaves(match.group(6)),
    }


# Per-fixture test plan: input words (in the reported leaf order)
# and expected output words (reported leaf order), plus an
# alternative input set for an if-selecting fixture.
PLANS: dict[str, dict[str, object]] = {
    "v2a_computed_scalar": {
        "runs": [
            {"in": [100], "out": [200]},
            {"in": [-7], "out": [-14]},
        ],
    },
    "v2a_computed_aggregate": {
        # Out layout: doubled@0 i64, sum@8 i64, flag@16 i32,
        # padding @20 == 0.
        "runs": [
            {"in": [100, 7], "out": [200, 107, 1]},
            {"in": [4, 0], "out": [8, 4, 1]},
        ],
    },
    "v2a_computed_enum": {
        # if (input.v > 10) -> High(tag 0) else Low(tag 1)
        "runs": [
            {"in": [50], "out": [0]},
            {"in": [1], "out": [1]},
        ],
    },
    "v2a_computed_payload_enum": {
        # Out.r: A{ p: Inner{a,b} } (tag 0) vs B{ z } (tag 1); the payload
        # union overlaps (A.a and B.z share @8). The reported leaf order is
        # tag@0, A.a@8, A.b@16, B.z@8, so the A run repeats @8's active word
        # and the B run expects A's inactive tail @16 to stay zero.
        "runs": [
            {"in": [1], "out": [0, 111, 222, 111]},
            {"in": [0], "out": [1, 555, 0, 555]},
        ],
    },
    "v2a_computed_if_let_return": {
        # if-let High arm ends in a two-branch value-returning if; the
        # implicit else (Low) falls through to the outer return.
        # inputs: level tag @0 (High=0, Low=1), w @8.
        "runs": [
            {"in": [0, 50], "out": [1]},
            {"in": [0, 5], "out": [2]},
            {"in": [1, 50], "out": [3]},
        ],
    },
}


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_v2a_computed_final_node_host.py <p6-producer> "
                   "<tests-golden-wasm-dir>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for V2-A computed-final execution")
        return SKIP
    producer = Path(argv[0])
    wasm_dir = Path(argv[1])

    host = (Path(__file__).resolve().parent.parent / "conformance" /
            "node_embedded_host_v2a_probe.mjs")

    for stem, plan in PLANS.items():
        source = wasm_dir / f"{stem}.ahfl"
        if not producer.is_file() or not source.is_file():
            return fail(f"missing producer or fixture {source}")
        with tempfile.TemporaryDirectory(prefix=f"ahfl-{stem}-") as td:
            wasm = Path(td) / f"{stem}.wasm"
            proc = subprocess.run(
                [str(producer), str(source), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if proc.returncode != 0:
                return fail(f"producer failed for {stem}: {proc.stderr}")
            try:
                report = parse_report(proc.stdout)
            except ValueError as exc:
                return fail(str(exc))
            mjs = host
            runs = plan["runs"]
            for run in runs:
                args = [
                    str(wasm),
                    str(report["input_base"]),
                    str(report["output_base"]),
                    str(report["output_size"]),
                    ",".join(f"{off}:{w}:{v}"
                             for (off, w), v in zip(report["inputs"], run["in"])),
                    ",".join(f"{off}:{w}:{v}"
                             for (off, w), v in zip(report["outputs"], run["out"])),
                ]
                ran = subprocess.run([node, str(mjs), *args],
                                     capture_output=True, text=True, timeout=60)
                if ran.returncode != 0:
                    return fail(
                        f"Node V2-A host failed for {stem} run {run['in']}: "
                        f"stdout={ran.stdout!r} stderr={ran.stderr!r}")

    print("OK: RFC 0026 P6-7 frame-bridge V2-A computed-final Node "
          "embedded-engine execution passed (scalar + nested aggregate + "
          "if-selected tag enum; real Node v22, NOT wasmtime)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
