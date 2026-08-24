#!/usr/bin/env python3
"""Infra target backend gate: snapshot + structural validation + determinism.

For each infra emit target (k8s-crd, openapi, terraform) this harness:

  1. Snapshot gate    -- emits the target and byte-compares stdout against the
                         captured golden file under tests/golden/infra/.
  2. Structural check -- parses the emitted artifact and asserts the required
                         top-level structure:
                           * openapi   -> valid JSON with keys openapi/info/paths
                           * k8s-crd   -> valid YAML/JSON with
                                          apiVersion/kind/metadata/spec
                           * terraform -> HCL containing the expected resource
                                          blocks
  3. Determinism      -- emits the target twice and asserts byte-identical
                         output (infra artifacts must be reproducible).

This is the pragmatic "snapshot gate" acceptance path: it does not require an
external validator (kubeconform / openapi-spec-validator) to be installed, but
still enforces that each artifact parses and carries its mandatory keys.

Usage:
    infra_target_gate.py <ahflc> <tests_dir>
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


def emit(ahflc: str, target: str, source: Path) -> str:
    """Run `ahflc emit <target> <source>` and return stdout only.

    Diagnostics (e.g. the detached-source-unit note) go to stderr and are
    intentionally excluded from the artifact under test.
    """
    proc = subprocess.run(
        [ahflc, "emit", target, str(source)],
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        raise SystemExit(
            f"FAIL: `ahflc emit {target}` exited {proc.returncode}\n"
            f"stderr:\n{proc.stderr}"
        )
    return proc.stdout


def load_yaml_or_json(text: str) -> dict:
    """Parse a k8s CRD document.

    Prefer PyYAML; fall back to json (the CRD is YAML, but a JSON-subset check
    still catches gross structural breakage when PyYAML is unavailable).
    """
    try:
        import yaml  # type: ignore

        return yaml.safe_load(text)
    except ImportError:
        # No PyYAML: verify the mandatory top-level keys appear as line-leading
        # mapping keys. This is weaker than a real parse but still fails on the
        # dangling-dash list-item corruption that motivated this gate.
        keys = {"apiVersion", "kind", "metadata", "spec"}
        found = {
            line.split(":", 1)[0].strip()
            for line in text.splitlines()
            if ":" in line and not line.startswith(" ")
        }
        missing = keys - found
        if missing:
            raise SystemExit(
                f"FAIL: k8s-crd missing top-level keys (no PyYAML): {missing}"
            )
        return {k: True for k in keys}


def check_openapi(text: str, golden: Path, ahflc: str, source: Path) -> None:
    actual_golden = golden.read_text()
    if text != actual_golden:
        raise SystemExit(
            f"FAIL: openapi snapshot mismatch vs {golden}\n"
            f"regenerate: {ahflc} emit openapi {source} > {golden}"
        )
    doc = json.loads(text)  # must be valid JSON
    for key in ("openapi", "info", "paths"):
        if key not in doc:
            raise SystemExit(f"FAIL: openapi missing required key '{key}'")
    if not doc["paths"]:
        raise SystemExit("FAIL: openapi 'paths' is empty")
    print("  ok openapi: snapshot match, valid JSON, keys openapi/info/paths")


def check_k8s(text: str, golden: Path, ahflc: str, source: Path) -> None:
    if text != golden.read_text():
        raise SystemExit(
            f"FAIL: k8s-crd snapshot mismatch vs {golden}\n"
            f"regenerate: {ahflc} emit k8s-crd {source} > {golden}"
        )
    doc = load_yaml_or_json(text)
    if not isinstance(doc, dict):
        raise SystemExit("FAIL: k8s-crd did not parse to a mapping")
    for key in ("apiVersion", "kind", "metadata", "spec"):
        if key not in doc:
            raise SystemExit(f"FAIL: k8s-crd missing required key '{key}'")
    print("  ok k8s-crd: snapshot match, valid YAML, keys "
          "apiVersion/kind/metadata/spec")


def check_terraform(text: str, golden: Path, ahflc: str, source: Path) -> None:
    if text != golden.read_text():
        raise SystemExit(
            f"FAIL: terraform snapshot mismatch vs {golden}\n"
            f"regenerate: {ahflc} emit terraform {source} > {golden}"
        )
    expected_blocks = [
        'resource "ahfl_workflow_node" "primary"',
        'resource "ahfl_workflow_node" "secondary"',
    ]
    for block in expected_blocks:
        if block not in text:
            raise SystemExit(f"FAIL: terraform missing expected block: {block}")
    # Balanced braces is a cheap HCL well-formedness signal.
    if text.count("{") != text.count("}"):
        raise SystemExit("FAIL: terraform HCL has unbalanced braces")
    print("  ok terraform: snapshot match, expected resource blocks, "
          "balanced braces")


def check_determinism(ahflc: str, target: str, source: Path) -> None:
    first = emit(ahflc, target, source)
    second = emit(ahflc, target, source)
    if first != second:
        raise SystemExit(
            f"FAIL: `emit {target}` is NON-DETERMINISTIC across two runs"
        )
    print(f"  ok {target}: deterministic (two emits byte-identical)")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    ahflc = sys.argv[1]
    tests_dir = Path(sys.argv[2])
    infra_dir = tests_dir / "golden" / "infra"
    source = infra_dir / "service_mesh.ahfl"

    if not source.exists():
        raise SystemExit(f"FAIL: fixture not found: {source}")

    print("=== infra target backend gate ===")

    targets = {
        "openapi": (infra_dir / "service_mesh.openapi.json", check_openapi),
        "k8s-crd": (infra_dir / "service_mesh.k8s-crd.yaml", check_k8s),
        "terraform": (infra_dir / "service_mesh.terraform.tf", check_terraform),
    }

    for target, (golden, checker) in targets.items():
        if not golden.exists():
            raise SystemExit(f"FAIL: golden not found: {golden}")
        text = emit(ahflc, target, source)
        checker(text, golden, ahflc, source)
        check_determinism(ahflc, target, source)

    print("=== all infra target gates passed ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
