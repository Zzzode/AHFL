#!/usr/bin/env python3
"""RFC 0026 P6-7 frame-bridge v2 rung V2-B Node execution evidence.

Real Node v22 (NOT wasmtime) evidence for in-module STRING (PtrLen)
construction and the packed input-payload arena:

  * rodata plans: the emitted computed-final module carries ONE additive
    Data(11) section whose single active segment initializes the rodata
    region [256,1024) with the hash-consed, byte-sorted, 8-aligned literal
    image; runv returns the inline PtrLen naming those bytes (struct field
    slots);
  * enum plan: a String-payload ENUM variant constructed in a computed
    final returns the payload PtrLen from rodata on the Hit branch and a
    provably zero inactive union on the Miss branch (exactly one Data
    section on both branches);
  * passthrough plan: a computed final copies the host-packed input-arena
    PtrLen to the output frame with ZERO Data sections; the output names
    the exact arena span and round-trips the bytes;
  * arena plans: bounded List<String(0,8)>(4) and a List of nested-struct
    elements carrying two bounded String slots pack ALL capacity elements
    through the reference one-cursor-per-element packer; the declared arena
    capacity is align8 over the per-OCCURRENCE schema sum (capacity*
    subtree), pinning the D6 multiplicity fix;
  * builtin-i64 plan: Decimal/Duration computed finals emit and materialize
    the compile-time i64 words (125 / 2000) but carry no Data section,
    pinning the deliberate emit-but-host-unobservable decision.

SKIP (77) when node is unavailable.
"""
from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

# Per-fixture rodata KAT image (hash-consed, 8-aligned).
RODATA_IMAGE_STRUCT = bytes.fromhex(
    "6e6f6e6500000000"  # "none" @0 + 4 zero pad
    "736f6d6500000000"  # "some" @8 + 4 zero pad
).hex()
RODATA_IMAGE_ENUM = bytes.fromhex(
    "6869740000000000"  # "hit" @0 + 5 zero pad
).hex()

# Struct-field literal plans (existing V2-B coverage).
RODATA_PLANS: list[tuple[str, int, str, str]] = [
    ("v2b_computed_string", 1, "some", "1"),
    ("v2b_computed_string", 0, "none", "0"),
    ("v2b_bounded_string", 1, "some", ""),
    ("v2b_bounded_string", 0, "none", ""),
]


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def parse_report(stdout: str) -> dict[str, object]:
    match = re.search(
        r"computed_final=1 input_base=(\d+) output_base=(\d+) "
        r"input_size=(\d+) output_size=(\d+)", stdout)
    if match is None:
        raise ValueError(f"no computed_final frame report in: {stdout!r}")
    report = {
        "input_base": int(match.group(1)),
        "output_base": int(match.group(2)),
        "input_size": int(match.group(3)),
        "output_size": int(match.group(4)),
    }
    ptrs = re.search(r"ptrlen_slots input=(\S*) output=(\S*)", stdout)
    if ptrs is None:
        raise ValueError(f"no ptrlen_slots report in: {stdout!r}")
    report["in_ptr_slots"] = [int(x) for x in ptrs.group(1).split(",") if x]
    report["out_ptr_slots"] = [int(x) for x in ptrs.group(2).split(",") if x]
    arena = re.search(
        r"frame_arena base=(\d+) cap=(\d+) rodata_base=(\d+) "
        r"rodata_extent=(\d+) placements=(\S*) containers=(.*)", stdout)
    if arena is None:
        raise ValueError(f"no frame_arena report in: {stdout!r}")
    report["arena_base"] = int(arena.group(1))
    report["arena_cap"] = int(arena.group(2))
    report["rodata_extent"] = int(arena.group(4))
    report["placements"] = arena.group(5)
    report["containers"] = arena.group(6).strip()
    return report


def run_node(node: str, host: Path, wasm: Path, args: list[str], stem: str,
             what: str) -> int | None:
    ran = subprocess.run(
        [node, str(host), str(wasm)] + args,
        capture_output=True, text=True, timeout=60,
    )
    if ran.returncode != 0:
        return fail(
            f"Node V2-B host failed for {stem} {what}: "
            f"stdout={ran.stdout!r} stderr={ran.stderr!r}")
    return None


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_v2b_string_literal_node_host.py <p6-producer> "
                    "<tests-golden-wasm-dir>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for V2-B string-literal execution")
        return SKIP
    producer = Path(argv[0])
    wasm_dir = Path(argv[1])
    conformance = Path(__file__).resolve().parent.parent / "conformance"
    host_rodata = conformance / "node_embedded_host_v2b_probe.mjs"
    host_enum = conformance / "node_embedded_host_v2b_enum_probe.mjs"
    host_passthrough = conformance / "node_embedded_host_v2b_passthrough_probe.mjs"
    host_arena = conformance / "node_embedded_host_v2b_arena_probe.mjs"
    host_v2a = conformance / "node_embedded_host_v2a_probe.mjs"

    def compile_fixture(stem: str, td: Path) -> tuple[Path, dict[str, object]]:
        source = wasm_dir / f"{stem}.ahfl"
        if not producer.is_file() or not source.is_file():
            raise RuntimeError(f"missing producer or fixture {source}")
        wasm_path = td / f"{stem}.wasm"
        proc = subprocess.run(
            [str(producer), str(source), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if proc.returncode != 0:
            raise RuntimeError(f"producer failed for {stem}: {proc.stderr}")
        return wasm_path, parse_report(proc.stdout)

    with tempfile.TemporaryDirectory(prefix="ahfl-v2b-node-") as tmp:
        td = Path(tmp)

        # 1. Struct-field rodata String construction (existing KAT lane).
        for stem, flag, expected, expected_int in RODATA_PLANS:
            wasm_path, _ = compile_fixture(stem, td)
            rc = run_node(node, host_rodata, wasm_path,
                          ["12288", str(flag), expected, expected_int,
                           RODATA_IMAGE_STRUCT], stem, f"flag={flag}")
            if rc is not None:
                return rc

        # 2. String-payload enum binding through a computed final: both
        #    branches, one Data section, payload from rodata on Hit.
        stem = "v2b_enum_string"
        wasm_path, report = compile_fixture(stem, td)
        if report["out_ptr_slots"] != [4]:
            return fail(f"{stem}: expected one enum payload PtrLen slot @4, got "
                        f"{report['out_ptr_slots']}")
        if report["rodata_extent"] != 8:
            return fail(f"{stem}: expected rodata extent 8, got "
                        f"{report['rodata_extent']}")
        # Miss first (tag 0, no payload), then Hit (tag 1, "hit" @payload slot).
        for flag, tag, expected in ((0, 0, ""), (1, 1, "hit")):
            rc = run_node(node, host_enum, wasm_path,
                          [str(report["input_base"]), str(report["output_base"]),
                           str(report["output_size"]), "4", str(flag),
                           str(tag), expected, RODATA_IMAGE_ENUM],
                          stem, f"flag={flag}")
            if rc is not None:
                return rc

        # 3. Input-arena passthrough: zero Data sections, output PtrLen names
        #    the packed arena span and round-trips the bytes.
        stem = "v2b_string_passthrough"
        wasm_path, report = compile_fixture(stem, td)
        if report["in_ptr_slots"] != [0] or report["out_ptr_slots"] != [0]:
            return fail(f"{stem}: expected input/output PtrLen slots @0, got "
                        f"{report['in_ptr_slots']}/{report['out_ptr_slots']}")
        packed = "arena-bytes"  # 11 bytes, inside the unbounded-pool arena.
        rc = run_node(node, host_passthrough, wasm_path,
                      [str(report["input_base"]), str(report["output_base"]),
                       "0", "0", str(report["arena_base"]),
                       str(report["arena_cap"]), packed], stem, "passthrough")
        if rc is not None:
            return rc

        # 4. Bounded-collection String arena multiplicity. Each element is one
        #    bare String slot (4 x 8-byte) or a nested struct with two String
        #    slots (3 x (4-byte + 2-byte)).
        arena_cases = [
            ("v2b_list_string_arena", ["aaaaaaaa"] * 4, 1),
            ("v2b_list_nested_string_arena",
             [["aaaa", "aa"]] * 3, 1),
        ]
        for stem, elements, expected_int in arena_cases:
            wasm_path, report = compile_fixture(stem, td)
            if not report["containers"]:
                return fail(f"{stem}: producer reported no input container")
            rc = run_node(node, host_arena, wasm_path,
                          [str(report["input_base"]), str(report["output_base"]),
                           str(report["arena_base"]), str(report["arena_cap"]),
                           report["placements"], report["containers"],
                           json.dumps(elements), str(expected_int)],
                          stem, "arena pack")
            if rc is not None:
                return rc

        # 5. Decimal/Duration i64 finals emit the words but carry no Data
        #    section (the v2a raw-word host drives runv; the structural
        #    eligibility gate pins the absent Data section).
        stem = "v2b_builtin_i64_final"
        wasm_path, report = compile_fixture(stem, td)
        rc = run_node(node, host_v2a, wasm_path,
                      [str(report["input_base"]), str(report["output_base"]),
                       str(report["output_size"]), "0:64:7",
                       "0:64:125,8:64:2000"], stem, "decimal/duration words")
        if rc is not None:
            return rc

    print("OK: RFC 0026 P6-7 frame-bridge V2-B in-module String(PtrLen) "
          "construction + input-payload arena Node execution passed (struct + "
          "enum-payload rodata, zero-Data input-arena passthrough, bounded-"
          "collection arena multiplicity, Decimal/Duration i64 words; real "
          "Node v22, NOT wasmtime)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
