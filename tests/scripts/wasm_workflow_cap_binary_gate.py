#!/usr/bin/env python3
"""RFC 0026 KR6.5 E4-B2-C capability-workflow binary/structural gate.

Always-on structural gate (NOT execution evidence). Drives the REAL ahflc CLI
against a committed package manifest (isolated into a TemporaryDirectory next to a
copy of the committed golden source) and checks:
  * byte identity between the ahflc package entry and the emit-only Core producer;
  * cross-run determinism of the CLI emission;
  * the import section is present (the ahfl_cap ABI);
  * the run_workflow tuple functype (i32,i32)->(i32,i32,i32) sits at type index 4 on
    EVERY workflow artifact (2026-10-11 run_workflow-retention decision): it is the
    workflow scheduler entry + node runner functype, a defined-function
    functype only -- no import may reference type 4, and every capability
    import must use the bridge functype (i32)->(i32,i32) at type index 6;
  * no import names the deleted ahfl_xcode transcode family;
  * the exec-manifest (AHFLXM) custom section carries the exact hardcoded golden
    payload and appears EXACTLY ONCE IMMEDIATELY BEFORE the wire-schema (AHFLWS)
    section, which is the module's FINAL section at EOF.
The run_workflow pending-latch first instruction, the 40-byte node-event record layout /
body-before-count write order, the checked-alloc fail/no-advance sequence, and the
shifted defined/export function indices are covered by the opcode/index assertions
in tests/unit/compiler/backends/core_wasm_codegen.cpp and the executed KR6.7
manifest-driven Node embedded host (tests/conformance/node_embedded_host.mjs,
driven by ahfl.conformance.wasm_node_differential); this gate does not re-assert
them.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Current execution manifest: magic, entry kind/id, and two scheduled nodes.
# Node 0 has one bridge capability; node 1 has none. No revision byte.
EXPECTED_AHFLXM = bytes.fromhex("4148464c584d000002000000010000000101010000")

WASM_HEADER = b"\x00asm\x01\x00\x00\x00"
CORE_LAYOUT_NAME = "ahfl.core-layout"
EXEC_MANIFEST_NAME = "ahfl.execution-manifest"
WIRE_SCHEMA_NAME = "ahfl.wire-schema"

PURE_P6_GOLDEN_NAME = "wh5c_instance_reuse_fanout.ahfl"


def fail(message: str) -> "NoReturn":
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_u32(data: bytes, offset: int) -> "tuple[int, int]":
    value = 0
    shift = 0
    for _ in range(5):
        if offset >= len(data):
            fail("truncated ULEB128")
        b = data[offset]
        offset += 1
        value |= (b & 0x7F) << shift
        if b & 0x80 == 0:
            return value, offset
        shift += 7
    fail("out-of-domain ULEB128")


def read_name(data: bytes, offset: int) -> "tuple[str, int]":
    size, offset = read_u32(data, offset)
    end = offset + size
    if end > len(data):
        fail("truncated wasm name")
    return data[offset:end].decode("utf-8"), end


def parse_sections(data: bytes):
    """Return an ordered list of (section_id, name_or_None, payload_after_name)."""
    if data[:8] != WASM_HEADER:
        fail("output is not wasm v1")
    sections = []
    offset = 8
    while offset < len(data):
        section_id = data[offset]
        offset += 1
        size, offset = read_u32(data, offset)
        end = offset + size
        if end > len(data):
            fail("truncated wasm section")
        if section_id == 0:
            name, name_end = read_name(data, offset)
            sections.append((0, name, data[name_end:end]))
        else:
            sections.append((section_id, None, data[offset:end]))
        offset = end
    return sections


def section_payload(sections, section_id: int) -> bytes:
    for sid, _, payload in sections:
        if sid == section_id:
            return payload
    fail(f"section id {section_id} is absent")


def function_types(payload: bytes) -> "list[tuple[list[int], list[int]]]":
    """Parse a wasm type section payload into [(params, results), ...]."""
    count, offset = read_u32(payload, 0)
    result: "list[tuple[list[int], list[int]]]" = []
    for _ in range(count):
        if offset >= len(payload) or payload[offset] != 0x60:
            fail("invalid function type")
        offset += 1
        param_count, offset = read_u32(payload, offset)
        params = list(payload[offset : offset + param_count])
        offset += param_count
        result_count, offset = read_u32(payload, offset)
        results = list(payload[offset : offset + result_count])
        offset += result_count
        result.append((params, results))
    if offset != len(payload):
        fail("trailing bytes in type section")
    return result


def imports(payload: bytes) -> "list[tuple[str, str, int, int]]":
    """Parse a wasm import section payload into [(module, field, kind, type_index), ...]."""
    count, offset = read_u32(payload, 0)
    result: "list[tuple[str, str, int, int]]" = []
    for _ in range(count):
        module, offset = read_name(payload, offset)
        field, offset = read_name(payload, offset)
        if offset >= len(payload):
            fail("truncated import")
        kind = payload[offset]
        offset += 1
        type_index, offset = read_u32(payload, offset)
        result.append((module, field, kind, type_index))
    if offset != len(payload):
        fail("trailing bytes in import section")
    return result


def emit_via_cli(ahflc: Path, manifest: Path, golden: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="ahfl-b2c-") as td:
        work = Path(td)
        # Isolated workspace: copy the committed manifest + committed golden using
        # the SAME file name / module name. No ahfl.toml is ever placed in the
        # golden directory (that would pollute the E1/E2 direct-source gates).
        shutil.copy(manifest, work / "ahfl.toml")
        shutil.copy(golden, work / golden.name)
        out_path = work / "output.wasm"
        proc = subprocess.run(
            [str(ahflc), "build", "--verify=off", "--manifest", str(work / "ahfl.toml"),
             "--target", "workflow", "--output", str(out_path)],
            capture_output=True,
            timeout=60,
        )
        if proc.returncode != 0:
            fail(f"ahflc build exited {proc.returncode}: "
                 f"{proc.stderr.decode(errors='replace')!r}")
        return out_path.read_bytes()


def emit_via_probe(probe: Path, golden: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="ahfl-b2c-probe-") as td:
        out = Path(td) / "probe.wasm"
        proc = subprocess.run(
            [str(probe), str(golden), str(out)],
            capture_output=True,
            timeout=60,
        )
        if proc.returncode != 0:
            fail(f"emit-only probe exited {proc.returncode}: "
                 f"{proc.stderr.decode(errors='replace')!r}")
        return out.read_bytes()


def emit_existing_package(ahflc: Path, manifest: Path) -> bytes:
    """Emit a committed package that already ships its own source alongside its
    manifest (used for the identity workflow structure)."""
    with tempfile.NamedTemporaryFile(suffix=".wasm", delete=False) as tmp:
        out_path = tmp.name
    try:
        proc = subprocess.run(
            [str(ahflc), "build", "--verify=off", "--manifest", str(manifest),
             "--target", "workflow", "--output", out_path],
            capture_output=True,
            timeout=60,
        )
        if proc.returncode != 0:
            fail(f"ahflc build exited {proc.returncode}: "
                 f"{proc.stderr.decode(errors='replace')!r}")
        return Path(out_path).read_bytes()
    finally:
        Path(out_path).unlink(missing_ok=True)


def main(argv: "list[str]") -> int:
    if len(argv) != 5:
        fail("usage: wasm_workflow_cap_binary_gate.py <ahflc> <probe> <manifest> "
             "<golden> <e3-identity-manifest>")
    ahflc = Path(argv[0])
    probe = Path(argv[1])
    manifest = Path(argv[2])
    golden = Path(argv[3])
    e3_manifest = Path(argv[4])

    # Exercise the current identity and pure-frame workflow structures.
    e3_bytes = emit_existing_package(ahflc, e3_manifest)
    p6_golden = golden.parent / PURE_P6_GOLDEN_NAME
    p6_bytes = emit_via_probe(probe, p6_golden)
    p6_sections = parse_sections(p6_bytes)
    if any(sid == 2 for sid, _, _ in p6_sections):
        fail("pure-P6 fixture unexpectedly carries an import section")

    cli_first = emit_via_cli(ahflc, manifest, golden)
    cli_second = emit_via_cli(ahflc, manifest, golden)
    if cli_first != cli_second:
        fail("capability-workflow CLI emission is non-deterministic")
    if not cli_first.startswith(WASM_HEADER):
        fail("capability-workflow CLI output is not a wasm v1 binary")

    # The real ahflc package entry and the emit-only Core producer must emit the
    # SAME bytes (E3 precedent): the CLI adds no re-encoding of its own.
    probe_bytes = emit_via_probe(probe, golden)
    if cli_first != probe_bytes:
        fail("package CLI entry and direct Core producer emitted different bytes")

    sections = parse_sections(cli_first)
    order = [(sid, name) for sid, name, _ in sections]

    # Import section (id 2) must be present (the ahfl_cap ABI).
    if not any(sid == 2 for sid, _ in order):
        fail("capability workflow has no import section")

    # RFC 0026 workflow run_workflow retention decision (2026-10-11): the workflow
    # scheduler entry + node runners KEEP the run_workflow tuple functype
    # (i32,i32)->(i32,i32,i32) at type index 4. It is a defined-function
    # functype ONLY -- the tuple type must never appear on an import, and the
    # deleted ahfl_xcode transcode import family must never reappear. The gate
    # asserts this on every workflow artifact it emits (identity, pure-P6,
    # capability): type index 4 is emitted unconditionally on the workflow
    # path, so a regression that moves or removes it breaks all three.
    i32 = 0x7F
    run_workflow_tuple = ([i32, i32], [i32, i32, i32])
    for label, artifact in (("E3 identity", e3_bytes),
                            ("pure-P6", p6_bytes),
                            ("capability", cli_first)):
        artifact_types = function_types(section_payload(parse_sections(artifact), 1))
        if len(artifact_types) <= 4 or artifact_types[4] != run_workflow_tuple:
            fail(f"{label} workflow: type index 4 is not the run_workflow tuple "
                 f"(i32,i32)->(i32,i32,i32): {artifact_types}")

    # The capability workflow's imports: every import is ahfl_cap.cap_<id>,
    # a function import (kind 0), on the bridge functype (i32)->(i32,i32) at
    # type index 6 -- never the run_workflow tuple at index 4, never ahfl_xcode.
    capability_types = function_types(section_payload(sections, 1))
    if (len(capability_types) <= 6
            or capability_types[6] != ([i32], [i32, i32])):
        fail("capability workflow: type index 6 is not the bridge functype "
             f"(i32)->(i32,i32): {capability_types}")
    for module, field, kind, type_index in imports(section_payload(sections, 2)):
        if module == "ahfl_xcode" or module.startswith("ahfl_xcode."):
            fail(f"deleted ahfl_xcode transcode import survives: {module}.{field}")
        if module != "ahfl_cap":
            fail(f"unexpected import module {module!r}; only ahfl_cap is defined")
        if kind != 0:
            fail(f"import {module}.{field} is not a function import (kind {kind})")
        if type_index == 4:
            fail(f"import {module}.{field} references the run_workflow tuple type 4; "
                 "the tuple is a defined-function functype only")
        if type_index != 6:
            fail(f"import {module}.{field} references type {type_index}; "
                 "capability imports must use the bridge functype at index 6")

    # The three custom sections must be exactly core-layout, AHFLXM, then
    # AHFLWS, and AHFLWS must be the module's FINAL section at EOF.
    # RFC 0026 dual-track elimination: the core-layout section is now present
    # on every module (all agents are P6-frame), not just pinned frame agents.
    customs = [(name, payload) for sid, name, payload in sections if sid == 0]
    custom_names = [name for name, _ in customs]
    if custom_names != [CORE_LAYOUT_NAME, EXEC_MANIFEST_NAME, WIRE_SCHEMA_NAME]:
        fail(f"unexpected custom section order: {custom_names}")
    if order[-1] != (0, WIRE_SCHEMA_NAME):
        fail("wire-schema section is not the module's final section")

    manifest_payload = customs[1][1]
    if manifest_payload != EXPECTED_AHFLXM:
        fail(f"AHFLXM payload mismatch: {manifest_payload.hex()}")
    if customs[2][1][:6] != b"AHFLWS":
        fail("wire-schema payload does not begin with AHFLWS")

    # The run_workflow opcode-level structural contract (pending-latch first instruction,
    # 40-byte event record writes preceding the event_count publish, checked
    # alloc) is locked in tests/unit/compiler/backends/core_wasm_codegen.cpp against the
    # same emitter; this gate owns the CLI-vs-producer byte identity + section /
    # import / AHFLXM-golden / AHFLWS-EOF structure.
    print("all KR6.5 E4-B2-C capability-workflow binary/structural gates passed "
          "(not execution evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
