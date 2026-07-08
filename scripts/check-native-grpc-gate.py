#!/usr/bin/env python3
"""Fail closed on native gRPC implementation before RFC0004 is implementation-ready."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RFC0004_REL = Path("docs") / "rfcs" / "0004-native-grpc-transport.zh.md"
DECISION_GATE_REL = Path("docs") / "plans" / "native-grpc-decision-gate.zh.md"
DECISION_EVIDENCE_REL = Path("docs") / "plans" / "native-grpc-decision-evidence.json"
SELF = Path(__file__).resolve()

ACCEPTED_STATUSES = {"accepted"}
IMPLEMENTATION_ALLOWED_STATUSES = {"implementing", "implemented", "stabilized"}
NO_GO_STATUSES = {"postponed", "rejected", "out-of-scope"}
EVIDENCE_SCHEMA = "ahfl.native_grpc_decision_evidence.v1"
EXPECTED_RFC = "0004-native-grpc-transport"
GATE_STATUSES = {"missing", "planned", "complete", "not_applicable"}
DECISION_STATES = {"pending", "go", "no-go"}
REQUIRED_GATES: tuple[str, ...] = (
    "runtime_owner_decision",
    "benchmark",
    "build_matrix",
    "dependency_policy",
    "feature_flag",
    "fallback_semantics",
    "test_strategy",
)
SKIP_DIRS = {
    ".git",
    ".cache",
    ".idea",
    ".vscode",
    "build",
    "cmake-build-debug",
    "cmake-build-release",
    "node_modules",
    "third_party",
}
SCAN_SUFFIXES = {
    ".cmake",
    ".cpp",
    ".cxx",
    ".cc",
    ".hpp",
    ".hxx",
    ".h",
    ".proto",
    ".py",
    ".sh",
    ".yml",
    ".yaml",
    ".txt",
}
FORBIDDEN_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    (
        "native gRPC build flag",
        re.compile(r"\bAHFL_ENABLE_GRPC_NATIVE\b"),
    ),
    (
        "native gRPC C++ include",
        re.compile(r"^\s*#\s*include\s*[<\"]grpcpp/", re.MULTILINE),
    ),
    (
        "native Protobuf C++ include",
        re.compile(r"^\s*#\s*include\s*[<\"]google/protobuf/", re.MULTILINE),
    ),
    (
        "CMake gRPC package lookup",
        re.compile(r"\bfind_package\s*\(\s*gRPC\b", re.IGNORECASE),
    ),
    (
        "CMake Protobuf package lookup",
        re.compile(r"\bfind_package\s*\(\s*Protobuf\b", re.IGNORECASE),
    ),
    (
        "CMake gRPC FetchContent wiring",
        re.compile(r"\bFetchContent_(?:Declare|MakeAvailable)\s*\([^)]*\bgrpc\b", re.IGNORECASE),
    ),
    (
        "CMake gRPC target link",
        re.compile(r"\bgRPC::grpc\+\+\b|\bgrpc::grpc\+\+\b"),
    ),
    (
        "CMake Protobuf target link",
        re.compile(r"\bprotobuf::libprotobuf\b|\bprotobuf::protoc\b"),
    ),
    (
        "native gRPC proto service contract",
        re.compile(r"\bservice\s+(?:LLMInference|StreamingInference|ToolCalling)\b"),
    ),
)


def display_path(root: Path, path: Path) -> Path:
    try:
        return path.relative_to(root)
    except ValueError:
        return path


def frontmatter_status(root: Path, path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    if not text.startswith("---\n"):
        raise RuntimeError(f"{display_path(root, path)}: missing YAML frontmatter")
    end = text.find("\n---", 4)
    if end == -1:
        raise RuntimeError(f"{display_path(root, path)}: unterminated YAML frontmatter")
    for line in text[4:end].splitlines():
        if line.startswith("status:"):
            return line.split(":", 1)[1].strip().strip("\"'")
    raise RuntimeError(f"{display_path(root, path)}: missing status field")


def relative_to_root(root: Path, path: Path) -> Path:
    return path.relative_to(root)


def should_scan(root: Path, path: Path, exempt_paths: set[Path]) -> bool:
    if path in exempt_paths:
        return False
    rel = relative_to_root(root, path)
    if any(part in SKIP_DIRS for part in rel.parts):
        return False
    if path.name in {"package-lock.json", "compile_commands.json"}:
        return False
    return path.suffix in SCAN_SUFFIXES or path.name in {"CMakeLists.txt"}


def is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def exempt_paths(root: Path) -> set[Path]:
    paths = {
        root / RFC0004_REL,
        root / DECISION_GATE_REL,
        root / DECISION_EVIDENCE_REL,
    }
    if is_relative_to(SELF, root):
        paths.add(SELF)
    return {path.resolve() for path in paths}


def read_decision_evidence(root: Path) -> tuple[dict[str, object] | None, list[str]]:
    path = root / DECISION_EVIDENCE_REL
    if not path.exists():
        return None, [f"{DECISION_EVIDENCE_REL}: missing native gRPC decision evidence file"]
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return None, [f"{DECISION_EVIDENCE_REL}:{exc.lineno}: invalid JSON: {exc.msg}"]
    if not isinstance(data, dict):
        return None, [f"{DECISION_EVIDENCE_REL}: top-level value must be an object"]
    return data, []


def validate_evidence_shape(evidence: dict[str, object]) -> list[str]:
    failures: list[str] = []
    if evidence.get("schema") != EVIDENCE_SCHEMA:
        failures.append(
            f"{DECISION_EVIDENCE_REL}: schema must be {EVIDENCE_SCHEMA!r}"
        )
    if evidence.get("rfc") != EXPECTED_RFC:
        failures.append(f"{DECISION_EVIDENCE_REL}: rfc must be {EXPECTED_RFC!r}")

    decision = evidence.get("decision")
    if not isinstance(decision, dict):
        failures.append(f"{DECISION_EVIDENCE_REL}: decision must be an object")
    else:
        state = decision.get("state")
        if state not in DECISION_STATES:
            failures.append(
                f"{DECISION_EVIDENCE_REL}: decision.state must be one of "
                f"{sorted(DECISION_STATES)}"
            )

    gates = evidence.get("gates")
    if not isinstance(gates, dict):
        failures.append(f"{DECISION_EVIDENCE_REL}: gates must be an object")
        return failures

    for gate in REQUIRED_GATES:
        gate_value = gates.get(gate)
        if not isinstance(gate_value, dict):
            failures.append(f"{DECISION_EVIDENCE_REL}: gates.{gate} must be an object")
            continue
        status = gate_value.get("status")
        if status not in GATE_STATUSES:
            failures.append(
                f"{DECISION_EVIDENCE_REL}: gates.{gate}.status must be one of "
                f"{sorted(GATE_STATUSES)}"
            )
        evidence_refs = gate_value.get("evidence")
        if evidence_refs is not None and (
            not isinstance(evidence_refs, list)
            or not all(isinstance(item, str) and item for item in evidence_refs)
        ):
            failures.append(
                f"{DECISION_EVIDENCE_REL}: gates.{gate}.evidence must be a list of "
                "non-empty strings when present"
            )
    return failures


def decision_object(evidence: dict[str, object]) -> dict[str, object]:
    decision = evidence.get("decision")
    return decision if isinstance(decision, dict) else {}


def gates_object(evidence: dict[str, object]) -> dict[str, object]:
    gates = evidence.get("gates")
    return gates if isinstance(gates, dict) else {}


def gate_is_complete(evidence: dict[str, object], gate: str) -> bool:
    gate_value = gates_object(evidence).get(gate)
    if not isinstance(gate_value, dict):
        return False
    evidence_refs = gate_value.get("evidence")
    return gate_value.get("status") == "complete" and isinstance(evidence_refs, list) and bool(evidence_refs)


def validate_status_transition(status: str, evidence: dict[str, object]) -> list[str]:
    failures: list[str] = []
    decision = decision_object(evidence)
    decision_state = decision.get("state")

    if status in ACCEPTED_STATUSES:
        if decision_state != "go":
            failures.append(
                f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires "
                "decision.state == 'go'"
            )
        if not gate_is_complete(evidence, "runtime_owner_decision"):
            failures.append(
                f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires a "
                "complete runtime_owner_decision gate with evidence"
            )

    if status in IMPLEMENTATION_ALLOWED_STATUSES:
        if decision_state != "go":
            failures.append(
                f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires "
                "decision.state == 'go'"
            )
        for gate in REQUIRED_GATES:
            if not gate_is_complete(evidence, gate):
                failures.append(
                    f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires "
                    f"complete gate {gate!r} with evidence"
                )

    if status in NO_GO_STATUSES:
        if decision_state != "no-go":
            failures.append(
                f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires "
                "decision.state == 'no-go'"
            )
        if not gate_is_complete(evidence, "runtime_owner_decision"):
            failures.append(
                f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires a "
                "complete runtime_owner_decision gate with evidence"
            )

    return failures


def scan_native_markers(root: Path, status: str) -> list[str]:
    failures: list[str] = []
    excluded_paths = exempt_paths(root)
    for path in sorted(root.rglob("*")):
        path = path.resolve()
        if not path.is_file() or not should_scan(root, path, excluded_paths):
            continue
        if re.search(r"\bnative[_-]grpc\b", path.name, re.IGNORECASE):
            failures.append(
                f"{relative_to_root(root, path)}: native gRPC source file is forbidden while RFC0004 "
                f"status is {status!r}. Complete the native gRPC decision gate first."
            )
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for label, pattern in FORBIDDEN_PATTERNS:
            match = pattern.search(text)
            if match is None:
                continue
            line = text.count("\n", 0, match.start()) + 1
            failures.append(
                f"{relative_to_root(root, path)}:{line}: {label} is forbidden while RFC0004 "
                f"status is {status!r}. Complete the native gRPC decision gate first."
            )
    return failures


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=ROOT,
        help="repository root to check; defaults to this script's repository",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    root = args.root.resolve()
    status = frontmatter_status(root, root / RFC0004_REL)

    evidence, evidence_failures = read_decision_evidence(root)
    failures = list(evidence_failures)
    if evidence is not None:
        failures.extend(validate_evidence_shape(evidence))
        failures.extend(validate_status_transition(status, evidence))

    marker_failures = scan_native_markers(root, status)
    if status not in IMPLEMENTATION_ALLOWED_STATUSES:
        failures.extend(marker_failures)
    elif marker_failures and failures:
        failures.extend(marker_failures)

    if failures:
        print("Native gRPC gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    if status in IMPLEMENTATION_ALLOWED_STATUSES:
        print(
            f"Native gRPC gate passed: RFC0004 status is {status}; "
            "decision evidence is complete enough for implementation markers."
        )
    else:
        print(f"Native gRPC gate passed: RFC0004 status is {status}; no native markers found.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
