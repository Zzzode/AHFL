#!/usr/bin/env python3
"""Infra target backend gate: snapshot + structural validation + determinism.

For each infra emit target (k8s-crd, openapi, terraform) and each fixture this
harness:

  1. Snapshot gate    -- emits the target and byte-compares stdout against the
                         captured golden file under tests/golden/infra/.
  2. Structural check -- parses the emitted artifact and asserts validity:
                           * openapi   -> valid JSON; every path carries
                                          requestBody + responses; every $ref
                                          resolves to a component schema
                           * k8s-crd   -> PyYAML parses EVERY document; doc
                                          count == agent count; metadata.name is
                                          RFC 1123-compliant and unique
                           * terraform -> HCL contains the expected resource
                                          blocks and depends_on DAG edges
  3. Determinism      -- emits the target twice and asserts byte-identical
                         output (infra artifacts must be reproducible).

This is the pragmatic "snapshot gate" acceptance path: it does not require an
external validator (kubeconform / openapi-spec-validator) to be installed, but
still enforces that each artifact parses and carries its mandatory content.

Usage:
    infra_target_gate.py <ahflc> <tests_dir>
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path


# RFC 1123 subdomain charset (lowercase alphanumerics, '-', '.'), bounded at
# 253 bytes.
RFC1123_SUBDOMAIN = re.compile(r"^[a-z0-9]([a-z0-9.\-]{0,251}[a-z0-9])?$")

# (fixture stem, expected agent count, expected terraform depends_on edges as
# "resource_type.name" substrings, optional structural expectations).
#   enum_components: component schema names that must be a string enum
#   no_request_body_paths: operation paths that must NOT carry a requestBody
#   no_context_ports: number of CRD documents expected without a context port
FIXTURES = [
    ("service_mesh", 1, ["ahfl_workflow_node.primary"], {}),
    ("multi_agent", 2, ["ahfl_workflow_node.first"], {}),
    (
        "stateless",
        1,
        [],
        {
            "no_request_body_paths": {"/infra/stateless/Ping"},
            "no_context_ports": 1,
        },
    ),
    (
        "palette",
        1,
        [],
        {
            "enum_components": ["infra__palette__Color"],
            "no_request_body_paths": {"/infra/palette/Pick"},
        },
    ),
]


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


def snapshot_ok(text: str, golden: Path, target: str, ahflc: str, source: Path) -> None:
    if text != golden.read_text():
        raise SystemExit(
            f"FAIL: {target} snapshot mismatch vs {golden}\n"
            f"regenerate: {ahflc} emit {target} {source} > {golden}"
        )


def collect_refs(node, refs):
    """Gather every {'$ref': ...} string reachable from a JSON node."""
    if isinstance(node, dict):
        ref = node.get("$ref")
        if isinstance(ref, str):
            refs.append(ref)
        for value in node.values():
            collect_refs(value, refs)
    elif isinstance(node, list):
        for item in node:
            collect_refs(item, refs)


def check_openapi(text: str, golden: Path, ahflc: str, source: Path,
                  expectations: dict) -> None:
    snapshot_ok(text, golden, "openapi", ahflc, source)
    doc = json.loads(text)  # must be valid JSON
    for key in ("openapi", "info", "paths"):
        if key not in doc:
            raise SystemExit(f"FAIL: openapi missing required key '{key}'")
    if not doc["paths"]:
        raise SystemExit("FAIL: openapi 'paths' is empty")

    components = doc.get("components", {}).get("schemas", {})

    # A C-style nominal enum component must be a string schema restricted to
    # its declared variants rather than the unconstrained schema {}.
    for name in expectations.get("enum_components", []):
        schema = components.get(name)
        if not isinstance(schema, dict) or schema.get("type") != "string":
            raise SystemExit(
                f"FAIL: openapi enum component {name!r} is not a string schema"
            )
        values = schema.get("enum")
        if not isinstance(values, list) or not values or not all(
            isinstance(v, str) for v in values
        ):
            raise SystemExit(
                f"FAIL: openapi enum component {name!r} has no string 'enum' list"
            )

    no_request_body = set(expectations.get("no_request_body_paths", set()))
    for path, operations in doc["paths"].items():
        if "::" in path:
            raise SystemExit(f"FAIL: openapi path {path!r} contains raw '::'")
        if not path.startswith("/"):
            raise SystemExit(f"FAIL: openapi path {path!r} is not absolute")
        for method, operation in operations.items():
            if path in no_request_body:
                # A zero-parameter capability must not advertise a required
                # empty request body.
                if "requestBody" in operation:
                    raise SystemExit(
                        f"FAIL: openapi {method.upper()} {path} unexpectedly "
                        f"emits a requestBody for a zero-parameter capability"
                    )
            elif "requestBody" not in operation:
                raise SystemExit(
                    f"FAIL: openapi {method.upper()} {path} missing requestBody"
                )
            responses = operation.get("responses", {})
            if "200" not in responses:
                raise SystemExit(
                    f"FAIL: openapi {method.upper()} {path} missing 200 response"
                )
            if "operationId" not in operation:
                raise SystemExit(
                    f"FAIL: openapi {method.upper()} {path} missing operationId"
                )

    refs: list[str] = []
    collect_refs(doc, refs)
    for ref in refs:
        if not ref.startswith("#/components/schemas/"):
            raise SystemExit(f"FAIL: openapi $ref {ref!r} is not a local schema ref")
        key = ref[len("#/components/schemas/"):]
        if key not in components:
            raise SystemExit(
                f"FAIL: openapi $ref {ref!r} does not resolve to a component schema"
            )

    print(
        f"  ok openapi: snapshot, valid JSON, "
        f"{len(doc['paths'])} paths, {len(refs)} $ref resolve"
    )


def check_k8s(text: str, golden: Path, ahflc: str, source: Path, agent_count: int,
              expectations: dict) -> None:
    snapshot_ok(text, golden, "k8s-crd", ahflc, source)
    try:
        import yaml  # type: ignore
    except ImportError:
        raise SystemExit(
            "FAIL: k8s-crd structural gate requires PyYAML (development dependency)"
        )

    documents = [doc for doc in yaml.safe_load_all(text) if doc is not None]
    if len(documents) != agent_count:
        raise SystemExit(
            f"FAIL: k8s-crd document count {len(documents)} != agent count {agent_count}"
        )

    no_context_expected = expectations.get("no_context_ports", 0)
    no_context_seen = 0
    names = []
    for doc in documents:
        if not isinstance(doc, dict):
            raise SystemExit("FAIL: a k8s-crd document did not parse to a mapping")
        for key in ("apiVersion", "kind", "metadata", "spec"):
            if key not in doc:
                raise SystemExit(f"FAIL: k8s-crd document missing required key '{key}'")
        metadata = doc["metadata"]
        name = metadata.get("name")
        if not isinstance(name, str) or not RFC1123_SUBDOMAIN.match(name):
            raise SystemExit(
                f"FAIL: k8s-crd metadata.name {name!r} is not RFC 1123 compliant"
            )
        if "::" in name or any(ch.isupper() for ch in name):
            raise SystemExit(
                f"FAIL: k8s-crd metadata.name {name!r} contains '::' or uppercase"
            )
        if len(name.encode("utf-8")) > 253:
            raise SystemExit(
                f"FAIL: k8s-crd metadata.name {name!r} exceeds 253 bytes"
            )
        # A stateless agent (omitted `context` clause) must not publish a
        # context port placeholder with a blank canonical type.
        properties = (
            doc.get("spec", {})
            .get("versions", [{}])[0]
            .get("schema", {})
            .get("openAPIV3Schema", {})
            .get("properties", {})
            .get("spec", {})
            .get("properties", {})
        )
        if "context" not in properties:
            no_context_seen += 1
        else:
            description = properties["context"].get("description", "")
            if description.rstrip().endswith("canonical type"):
                raise SystemExit(
                    f"FAIL: k8s-crd {name!r} context port has a blank canonical type"
                )
        # The fully qualified identity must survive, but only outside RFC 1123
        # identity fields.
        annotations = metadata.get("annotations", {})
        if "ahfl.io/agent" not in annotations:
            raise SystemExit(
                f"FAIL: k8s-crd document {name!r} missing ahfl.io/agent annotation"
            )
        names.append(name)

    if no_context_seen != no_context_expected:
        raise SystemExit(
            f"FAIL: k8s-crd context-port count {no_context_seen} != expected "
            f"{no_context_expected}"
        )

    if len(set(names)) != len(names):
        raise SystemExit(f"FAIL: k8s-crd metadata.names are not unique: {names}")

    print(
        f"  ok k8s-crd: snapshot, {len(documents)} YAML docs parse, "
        f"{len(names)} unique RFC 1123 metadata.name (<=253 bytes)"
    )


def check_terraform(text: str, golden: Path, ahflc: str, source: Path, edges) -> None:
    snapshot_ok(text, golden, "terraform", ahflc, source)
    if text.count("{") != text.count("}"):
        raise SystemExit("FAIL: terraform HCL has unbalanced braces")
    for edge in edges:
        if f"depends_on = [{edge}]" not in text:
            raise SystemExit(f"FAIL: terraform missing expected depends_on edge {edge}")
    print(f"  ok terraform: snapshot, balanced braces, {len(edges)} depends_on edge(s)")


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

    print("=== infra target backend gate ===")

    for stem, agent_count, edges, expectations in FIXTURES:
        source = infra_dir / f"{stem}.ahfl"
        if not source.exists():
            raise SystemExit(f"FAIL: fixture not found: {source}")
        print(f"--- {stem} ---")

        targets = {
            "openapi": (
                infra_dir / f"{stem}.openapi.json",
                lambda text, golden: check_openapi(
                    text, golden, ahflc, source, expectations
                ),
            ),
            "k8s-crd": (
                infra_dir / f"{stem}.k8s-crd.yaml",
                lambda text, golden: check_k8s(
                    text, golden, ahflc, source, agent_count, expectations
                ),
            ),
            "terraform": (
                infra_dir / f"{stem}.terraform.tf",
                lambda text, golden: check_terraform(text, golden, ahflc, source, edges),
            ),
        }

        for target, (golden, checker) in targets.items():
            if not golden.exists():
                raise SystemExit(f"FAIL: golden not found: {golden}")
            text = emit(ahflc, target, source)
            checker(text, golden)
            check_determinism(ahflc, target, source)

    print("=== all infra target gates passed ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
