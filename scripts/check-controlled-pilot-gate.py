#!/usr/bin/env python3
"""Evaluate the bounded controlled-pilot gate from machine evidence."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from ahfl_source_revision import compute_source_revision


DEFAULT_ROOT = Path(__file__).resolve().parents[1]
EXPECTED_FAULTS = ("disconnect", "rate_limit", "timeout", "partial_response")
EXPECTED_TEST_KEYS = (
    "process_crash",
    "recovery_schema",
    "otel_adapter",
    "provider_budget",
)
EXPECTED_RECOVERY_POLICY = "reject unknown and legacy schemas"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT, help="repository root")
    parser.add_argument(
        "--require-ready",
        action="store_true",
        help="return non-zero unless controlled-pilot evidence is complete and passing",
    )
    return parser.parse_args()


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def is_positive_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def is_non_negative_number(value: object) -> bool:
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and value >= 0
    )


def validate_contract(config: Any) -> list[str]:
    failures: list[str] = []
    if not isinstance(config, dict):
        return ["controlled-pilot gate config must be a JSON object"]
    if config.get("schema") != "ahfl.controlled-pilot-gate.v1":
        failures.append(
            "controlled-pilot gate config schema must be ahfl.controlled-pilot-gate.v1"
        )
    if not str(config.get("reference_workflow", "")).strip():
        failures.append("reference_workflow must not be empty")

    evidence = config.get("evidence")
    if not isinstance(evidence, dict):
        failures.append("evidence must be an object")
    else:
        if not str(evidence.get("path", "")).strip():
            failures.append("evidence.path must not be empty")
        if not str(evidence.get("schema", "")).strip():
            failures.append("evidence.schema must not be empty")

    minimums = config.get("minimums")
    if not isinstance(minimums, dict):
        failures.append("minimums must be an object")
    else:
        if not str(minimums.get("soak_kind", "")).strip():
            failures.append("minimums.soak_kind must not be empty")
        if not is_positive_int(minimums.get("iterations")):
            failures.append("minimums.iterations must be a positive integer")
        if not is_non_negative_number(minimums.get("duration_seconds")):
            failures.append("minimums.duration_seconds must be non-negative")

    faults = config.get("required_faults")
    if not isinstance(faults, list) or any(
        not isinstance(fault, str) or not fault for fault in faults
    ):
        failures.append("required_faults must be a non-empty string array")
    elif tuple(faults) != EXPECTED_FAULTS:
        failures.append(
            "required_faults must be exactly: " + ", ".join(EXPECTED_FAULTS)
        )

    tests = config.get("required_tests")
    if not isinstance(tests, dict):
        failures.append("required_tests must be an object")
    else:
        missing = [key for key in EXPECTED_TEST_KEYS if not str(tests.get(key, "")).strip()]
        extras = sorted(set(tests) - set(EXPECTED_TEST_KEYS))
        if missing:
            failures.append("required_tests missing keys: " + ", ".join(missing))
        if extras:
            failures.append("required_tests has unknown keys: " + ", ".join(extras))
    return failures


def validate_evidence(config: dict[str, Any], evidence: Any) -> list[str]:
    failures: list[str] = []
    if not isinstance(evidence, dict):
        return ["evidence must be a JSON object"]
    expected_evidence = config["evidence"]
    if evidence.get("schema") != expected_evidence["schema"]:
        failures.append(
            f"schema mismatch: found {evidence.get('schema')!r}, "
            f"expected {expected_evidence['schema']!r}"
        )
    if evidence.get("status") != "passed":
        failures.append(f"evidence status is {evidence.get('status')!r}, expected 'passed'")
    if not str(evidence.get("source_revision", "")).strip():
        failures.append("source_revision must not be empty")
    if evidence.get("reference_workflow") != config["reference_workflow"]:
        failures.append(
            "reference_workflow mismatch: "
            f"found {evidence.get('reference_workflow')!r}"
        )

    minimums = config["minimums"]
    soak = evidence.get("soak")
    if not isinstance(soak, dict):
        failures.append("soak must be an object")
    else:
        if soak.get("kind") != minimums["soak_kind"]:
            failures.append(
                f"soak.kind is {soak.get('kind')!r}, expected {minimums['soak_kind']!r}"
            )
        iterations = soak.get("iterations")
        if not is_positive_int(iterations) or iterations < minimums["iterations"]:
            failures.append(
                f"soak.iterations must be at least {minimums['iterations']}"
            )
        declared_iterations = soak.get("minimum_iterations")
        if (
            not is_positive_int(declared_iterations)
            or declared_iterations < minimums["iterations"]
        ):
            failures.append(
                "soak.minimum_iterations must preserve the contract threshold "
                f"{minimums['iterations']}"
            )
        duration = soak.get("duration_seconds")
        if (
            not is_non_negative_number(duration)
            or duration < minimums["duration_seconds"]
        ):
            failures.append(
                f"soak.duration_seconds must be at least {minimums['duration_seconds']}"
            )
        declared_duration = soak.get("minimum_duration_seconds")
        if (
            not is_non_negative_number(declared_duration)
            or declared_duration < minimums["duration_seconds"]
        ):
            failures.append(
                "soak.minimum_duration_seconds must preserve the contract threshold "
                f"{minimums['duration_seconds']}"
            )
        event_count = soak.get("stable_event_count")
        if not is_positive_int(event_count):
            failures.append("soak.stable_event_count must be a positive integer")
        request_count = soak.get("provider_request_count")
        if not is_positive_int(request_count) or request_count != iterations:
            failures.append(
                "soak.provider_request_count must equal soak.iterations"
            )

    faults = evidence.get("network_faults")
    if not isinstance(faults, dict):
        failures.append("network_faults must be an object")
    else:
        for fault in config["required_faults"]:
            result = faults.get(fault)
            if not isinstance(result, dict):
                failures.append(f"network_faults.{fault} is missing")
                continue
            if result.get("status") != "passed":
                failures.append(
                    f"network_faults.{fault}.status is "
                    f"{result.get('status')!r}, expected 'passed'"
                )
            if not is_positive_int(result.get("request_count")):
                failures.append(
                    f"network_faults.{fault}.request_count must be a positive integer"
                )

    tests = config["required_tests"]
    evidence_test_fields = {
        "process_crash": "process_crash_test",
        "recovery_schema": "recovery_schema_test",
        "otel_adapter": "otel_adapter_test",
        "provider_budget": "provider_budget_test",
    }
    for key, field in evidence_test_fields.items():
        if evidence.get(field) != tests[key]:
            failures.append(
                f"{field} is {evidence.get(field)!r}, expected {tests[key]!r}"
            )
    if evidence.get("recovery_schema_policy") != EXPECTED_RECOVERY_POLICY:
        failures.append(
            "recovery_schema_policy must be "
            f"{EXPECTED_RECOVERY_POLICY!r}"
        )
    return failures


def report(
    config: dict[str, Any],
    status: str,
    failures: list[str],
    source_revision: str | None,
    current_source_revision: str,
) -> dict[str, Any]:
    return {
        "schema": "ahfl.controlled-pilot-gate-report.v1",
        "status": status,
        "source_revision": source_revision,
        "current_source_revision": current_source_revision,
        "reference_workflow": config["reference_workflow"],
        "evidence": {
            "path": config["evidence"]["path"],
            "schema": config["evidence"]["schema"],
        },
        "minimums": config["minimums"],
        "required_faults": config["required_faults"],
        "required_tests": config["required_tests"],
        "failures": failures,
    }


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    config_path = root / "config/controlled-pilot-gate.json"
    try:
        config = load_json(config_path)
    except (OSError, json.JSONDecodeError) as error:
        print(f"controlled-pilot gate contract error: {error}", file=sys.stderr)
        return 2

    contract_failures = validate_contract(config)
    if contract_failures:
        for failure in contract_failures:
            print(f"controlled-pilot gate contract error: {failure}", file=sys.stderr)
        return 2

    try:
        current_source_revision = compute_source_revision(root)
    except RuntimeError as error:
        print(f"controlled-pilot gate source revision error: {error}", file=sys.stderr)
        return 2

    evidence_path = root / config["evidence"]["path"]
    if not evidence_path.is_file():
        value = report(
            config,
            "missing_evidence",
            ["evidence file does not exist"],
            None,
            current_source_revision,
        )
        print(json.dumps(value, indent=2, sort_keys=True))
        return 1 if args.require_ready else 0

    try:
        evidence = load_json(evidence_path)
    except (OSError, json.JSONDecodeError) as error:
        failures = [f"evidence is not valid JSON: {error}"]
        source_revision = None
    else:
        failures = validate_evidence(config, evidence)
        source_revision_value = evidence.get("source_revision")
        source_revision = (
            source_revision_value.strip()
            if isinstance(source_revision_value, str)
            else None
        )
        if source_revision and source_revision != current_source_revision:
            failures.append(
                "controlled-pilot evidence is stale for the current source revision: "
                f"found {source_revision!r}, expected {current_source_revision!r}"
            )
    status = "failed" if failures else "ready"
    print(
        json.dumps(
            report(
                config,
                status,
                failures,
                source_revision,
                current_source_revision,
            ),
            indent=2,
            sort_keys=True,
        )
    )
    return 1 if args.require_ready and status != "ready" else 0


if __name__ == "__main__":
    raise SystemExit(main())
