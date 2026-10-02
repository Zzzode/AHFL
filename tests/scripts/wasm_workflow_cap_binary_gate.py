#!/usr/bin/env python3
"""RFC 0026 KR6.5 E4-B2-C capability-workflow binary/structural gate.

Always-on structural gate (NOT execution evidence). Drives the REAL ahflc CLI
against a committed package manifest (isolated into a TemporaryDirectory next to a
copy of the committed golden source, per the B4 ruling) and locks, precisely:
  * the no-capability E3 identity workflow byte-freeze (exact size + SHA-256)
    across the B2-C landing;
  * the pure-P6 workflow byte-freeze (exact size + SHA-256): WH-5c.5 adds the
    per-node output stash stores ONLY when the workflow has an opaque node, so
    a pure-P6 module must stay byte-identical -- the freeze fails if the
    has_opaque_nodes / stash guards ever regress and stash bytes leak into a
    pure-P6 emission;
  * byte identity between the ahflc package entry and the emit-only Core producer;
  * cross-run determinism of the CLI emission;
  * the import section is present (the ahfl_cap ABI);
  * the exec-manifest (AHFLXM) custom section carries the exact hardcoded golden
    payload and appears EXACTLY ONCE IMMEDIATELY BEFORE the wire-schema (AHFLWS)
    section, which is the module's FINAL section at EOF.
The run2 pending-latch first instruction, the 40-byte node-event record layout /
body-before-count write order, the checked-alloc fail/no-advance sequence, and the
shifted defined/export function indices are covered by the opcode/index assertions
in tests/unit/compiler/backends/core_wasm_codegen.cpp and the executed KR6.7
manifest-driven Node embedded host (tests/conformance/node_embedded_host.mjs,
driven by ahfl.conformance.wasm_node_differential); this gate does not re-assert
them.
"""

import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# The hardcoded, independently-reviewed canonical AHFLXM payload for the golden
# capability workflow: magic "AHFLXM", version 2 (WH-5b.2 manifest v2),
# entry{kind 0, id 0}, node_count 2; node0 id=0 sched=0 cap_call=1 capability=0
# source_symbol=1 bridge_site_count=0; node1 id=1 sched=1 cap_call=0
# bridge_site_count=0. The per-node bridge_site_count(u8) is appended AFTER the
# capabilities array in v2; both nodes are opaque-lane (zero bridge sites).
EXPECTED_AHFLXM = bytes.fromhex("4148464c584d0200000200000100010001010000")

WASM_HEADER = b"\x00asm\x01\x00\x00\x00"
EXEC_MANIFEST_NAME = "ahfl.wasm-exec-manifest.v1"
WIRE_SCHEMA_NAME = "ahfl.wire-schema.v1"

# Hard-lock the no-capability E3 identity workflow byte-freeze. WH-5c.5
# (design 12.15.19) intentionally adds the per-node output stash stores to
# every opaque node, so the identity workflow grew from 470 to 502 bytes
# (2 nodes x 16 bytes of i32.store pairs); only a pure-P6 workflow (no
# opaque nodes) stays byte-identical. Independently reproduced from the
# WH-5c.5 artifact.
E3_IDENTITY_SIZE = 502
E3_IDENTITY_SHA256 = "8eb0bb9f9a64a0930c44e50939e22e40dfd4aac6fdde1359dd16c55544b3d3ff"

# Hard-lock the pure-P6 workflow byte-freeze. WH-5c.5 reserves the stash
# region and emits the per-node (ptr,len) stores ONLY when the schedule has an
# opaque node (workflow_has_opaque_nodes / stash_base != 0 guards). This
# fixture is a two-node all-P6 fan-out with zero imports; its bytes must not
# move when the stash machinery lands, so this freeze fails loudly if the
# guards regress and stash bytes leak into a pure-P6 emission. Independently
# reproduced from the WH-5c.5 artifact.
PURE_P6_GOLDEN_NAME = "wh5c_instance_reuse_fanout.ahfl"
PURE_P6_SIZE = 1513
PURE_P6_SHA256 = "ca80b9d1bb02e3e0e1480de1ccb0c5bc18e25f0dee766a1632cc6ab3fe39636b"


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


def emit_via_cli(ahflc: Path, manifest: Path, golden: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="ahfl-b2c-") as td:
        work = Path(td)
        # Isolated workspace: copy the committed manifest + committed golden using
        # the SAME file name / module name. No ahfl.toml is ever placed in the
        # golden directory (that would pollute the E1/E2 direct-source gates).
        shutil.copy(manifest, work / "ahfl.toml")
        shutil.copy(golden, work / golden.name)
        proc = subprocess.run(
            [str(ahflc), "emit", "wasm", "--manifest", str(work / "ahfl.toml"),
             "--target", "workflow"],
            capture_output=True,
            timeout=60,
        )
        if proc.returncode != 0:
            fail(f"ahflc emit exited {proc.returncode}: "
                 f"{proc.stderr.decode(errors='replace')!r}")
        return proc.stdout


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
    manifest (used for the E3 identity byte-freeze canary)."""
    proc = subprocess.run(
        [str(ahflc), "emit", "wasm", "--manifest", str(manifest), "--target", "workflow"],
        capture_output=True,
        timeout=60,
    )
    if proc.returncode != 0:
        fail(f"ahflc emit exited {proc.returncode}: "
             f"{proc.stderr.decode(errors='replace')!r}")
    return proc.stdout


def main(argv: "list[str]") -> int:
    if len(argv) != 5:
        fail("usage: wasm_workflow_cap_binary_gate.py <ahflc> <probe> <manifest> "
             "<golden> <e3-identity-manifest>")
    ahflc = Path(argv[0])
    probe = Path(argv[1])
    manifest = Path(argv[2])
    golden = Path(argv[3])
    e3_manifest = Path(argv[4])

    # Hard-lock the no-capability E3 identity workflow byte-freeze: the B2-C
    # landing must not perturb its bytes (import_count == 0).
    e3_bytes = emit_existing_package(ahflc, e3_manifest)
    if len(e3_bytes) != E3_IDENTITY_SIZE:
        fail(f"E3 identity workflow size changed: {len(e3_bytes)} != {E3_IDENTITY_SIZE}")
    e3_digest = hashlib.sha256(e3_bytes).hexdigest()
    if e3_digest != E3_IDENTITY_SHA256:
        fail(f"E3 identity workflow byte-freeze broken: {e3_digest}")

    # Hard-lock the pure-P6 workflow byte-freeze (WH-5c.5 stash guards):
    # zero imports, zero stash bytes, byte-identical emission.
    p6_golden = golden.parent / PURE_P6_GOLDEN_NAME
    p6_bytes = emit_via_probe(probe, p6_golden)
    if len(p6_bytes) != PURE_P6_SIZE:
        fail(f"pure-P6 workflow size changed: {len(p6_bytes)} != {PURE_P6_SIZE}")
    p6_digest = hashlib.sha256(p6_bytes).hexdigest()
    if p6_digest != PURE_P6_SHA256:
        fail(f"pure-P6 workflow byte-freeze broken: {p6_digest}")
    p6_sections = parse_sections(p6_bytes)
    if any(sid == 2 for sid, _, _ in p6_sections):
        fail("pure-P6 freeze fixture unexpectedly carries an import section")

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

    # The two custom sections must be exactly AHFLXM then AHFLWS, and AHFLWS must
    # be the module's FINAL section at EOF.
    customs = [(name, payload) for sid, name, payload in sections if sid == 0]
    custom_names = [name for name, _ in customs]
    if custom_names != [EXEC_MANIFEST_NAME, WIRE_SCHEMA_NAME]:
        fail(f"unexpected custom section order: {custom_names}")
    if order[-1] != (0, WIRE_SCHEMA_NAME):
        fail("wire-schema section is not the module's final section")

    manifest_payload = customs[0][1]
    if manifest_payload != EXPECTED_AHFLXM:
        fail(f"AHFLXM payload mismatch: {manifest_payload.hex()}")
    if customs[1][1][:6] != b"AHFLWS":
        fail("wire-schema payload does not begin with AHFLWS")

    # The run2 opcode-level structural contract (pending-latch first instruction,
    # 40-byte event record writes preceding the event_count publish, checked
    # alloc) is locked in tests/unit/compiler/backends/core_wasm_codegen.cpp against the
    # same emitter; this gate owns the CLI-vs-producer byte identity + section /
    # import / AHFLXM-golden / AHFLWS-EOF structure.
    print("all KR6.5 E4-B2-C capability-workflow binary/structural gates passed "
          "(not execution evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
