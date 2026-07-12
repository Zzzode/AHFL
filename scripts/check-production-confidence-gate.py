#!/usr/bin/env python3
"""Evaluate hour-scale soak and memory-trend production confidence evidence."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

from ahfl_source_revision import compute_source_revision


DEFAULT_ROOT = Path(__file__).resolve().parents[1]
METRIC_KEYS = ("peak_rss", "allocator_in_use", "allocator_reserved")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--require-ready", action="store_true")
    return parser.parse_args()


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def finite_number(value: object) -> bool:
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(float(value))
    )


def positive_integer(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def validate_contract(value: Any) -> list[str]:
    failures: list[str] = []
    if not isinstance(value, dict):
        return ["production confidence contract must be an object"]
    if value.get("schema") != "ahfl.production-confidence-gate.v1":
        failures.append("contract schema must be ahfl.production-confidence-gate.v1")
    evidence = value.get("evidence")
    if not isinstance(evidence, dict):
        failures.append("evidence must be an object")
    else:
        if not str(evidence.get("path", "")).strip():
            failures.append("evidence.path must not be empty")
        if not str(evidence.get("schema", "")).strip():
            failures.append("evidence.schema must not be empty")
    duration = value.get("minimum_duration_seconds")
    if not finite_number(duration) or float(duration) < 3600:
        failures.append("minimum_duration_seconds must be at least 3600")
    if not positive_integer(value.get("minimum_iterations")):
        failures.append("minimum_iterations must be a positive integer")
    if not positive_integer(value.get("minimum_provider_retries")):
        failures.append("minimum_provider_retries must be a positive integer")
    ratio = value.get("maximum_last_quartile_growth_ratio")
    if not finite_number(ratio) or float(ratio) < 0:
        failures.append("maximum_last_quartile_growth_ratio must be non-negative")
    return failures


def validate_metric(
    name: str, value: object, maximum_growth_ratio: float
) -> list[str]:
    failures: list[str] = []
    if not isinstance(value, dict):
        return [f"{name} must be an object"]
    if not positive_integer(value.get("samples")):
        failures.append(f"{name}.samples must be positive")
    for field in (
        "minimum_bytes",
        "maximum_bytes",
        "mean_bytes",
        "slope_bytes_per_iteration",
        "first_quartile_mean_bytes",
        "last_quartile_mean_bytes",
    ):
        if not finite_number(value.get(field)):
            failures.append(f"{name}.{field} must be finite")
    if failures:
        return failures
    minimum = float(value["minimum_bytes"])
    maximum = float(value["maximum_bytes"])
    first = float(value["first_quartile_mean_bytes"])
    last = float(value["last_quartile_mean_bytes"])
    if minimum < 0 or maximum < minimum:
        failures.append(f"{name} byte bounds are inconsistent")
    if name == "peak_rss" and minimum <= 0:
        failures.append("peak_rss minimum must be positive")
    if first < 0 or last < 0:
        failures.append(f"{name} quartile means must be non-negative")
    if first == 0:
        if last != 0:
            failures.append(f"{name} cannot grow from zero baseline")
    elif (last - first) / first > maximum_growth_ratio:
        failures.append(
            f"{name} last-quartile growth exceeds {maximum_growth_ratio:.3f}"
        )
    return failures


def validate_evidence(
    contract: dict[str, Any],
    evidence: Any,
    current_revision: str,
) -> tuple[list[str], str | None]:
    failures: list[str] = []
    if not isinstance(evidence, dict):
        return ["evidence must be an object"], None
    expected = contract["evidence"]["schema"]
    if evidence.get("schema") != expected:
        failures.append(f"schema mismatch: found {evidence.get('schema')!r}")
    if evidence.get("status") != "passed":
        failures.append(f"status is {evidence.get('status')!r}, expected 'passed'")
    revision = evidence.get("source_revision")
    if not isinstance(revision, str) or not revision.strip():
        failures.append("source_revision must not be empty")
        revision = None
    elif revision != current_revision:
        failures.append(
            "production confidence evidence is stale for the current source revision: "
            f"found {revision!r}, expected {current_revision!r}"
        )
    if evidence.get("kind") != "hour-scale":
        failures.append("kind must be 'hour-scale'")
    if evidence.get("process_model") != "single-long-lived-worker":
        failures.append("process_model must be 'single-long-lived-worker'")
    if evidence.get("reference_workflow") != "examples/execution-demo":
        failures.append("reference_workflow mismatch")
    duration = evidence.get("duration_seconds")
    if (
        not finite_number(duration)
        or float(duration) < float(contract["minimum_duration_seconds"])
    ):
        failures.append(
            f"duration_seconds must be at least {contract['minimum_duration_seconds']}"
        )
    declared_duration = evidence.get("minimum_duration_seconds")
    if (
        not finite_number(declared_duration)
        or float(declared_duration) < float(contract["minimum_duration_seconds"])
    ):
        failures.append("evidence minimum duration weakens the contract")
    iterations = evidence.get("iterations")
    if (
        not positive_integer(iterations)
        or iterations < contract["minimum_iterations"]
    ):
        failures.append(
            f"iterations must be at least {contract['minimum_iterations']}"
        )
    if not positive_integer(evidence.get("stable_event_count")):
        failures.append("stable_event_count must be positive")
    provider_retry_count = evidence.get("provider_retry_count")
    if (
        not positive_integer(provider_retry_count)
        or provider_retry_count < contract["minimum_provider_retries"]
    ):
        failures.append(
            "provider_retry_count must be at least "
            f"{contract['minimum_provider_retries']}"
        )
    if (
        positive_integer(iterations)
        and positive_integer(provider_retry_count)
        and evidence.get("provider_request_count") != iterations + provider_retry_count
    ):
        failures.append(
            "provider_request_count must equal iterations plus provider_retry_count"
        )
    if not finite_number(evidence.get("throughput_runs_per_second")) or float(
        evidence.get("throughput_runs_per_second", 0)
    ) <= 0:
        failures.append("throughput_runs_per_second must be positive and finite")
    latency = evidence.get("latency_seconds")
    if not isinstance(latency, dict) or any(
        not finite_number(latency.get(field)) or float(latency.get(field, -1)) < 0
        for field in ("minimum", "maximum", "mean")
    ):
        failures.append("latency_seconds must contain finite non-negative values")
    ratio = float(contract["maximum_last_quartile_growth_ratio"])
    for key in METRIC_KEYS:
        failures.extend(validate_metric(key, evidence.get(key), ratio))
    return failures, revision


def make_report(
    contract: dict[str, Any],
    status: str,
    failures: list[str],
    source_revision: str | None,
    current_revision: str,
) -> dict[str, Any]:
    return {
        "schema": "ahfl.production-confidence-gate-report.v1",
        "status": status,
        "source_revision": source_revision,
        "current_source_revision": current_revision,
        "evidence": contract["evidence"],
        "minimum_duration_seconds": contract["minimum_duration_seconds"],
        "minimum_iterations": contract["minimum_iterations"],
        "minimum_provider_retries": contract["minimum_provider_retries"],
        "maximum_last_quartile_growth_ratio": contract[
            "maximum_last_quartile_growth_ratio"
        ],
        "failures": failures,
    }


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    try:
        contract = load_json(root / "config/production-confidence-gate.json")
    except (OSError, json.JSONDecodeError) as error:
        print(f"production confidence contract error: {error}", file=sys.stderr)
        return 2
    contract_failures = validate_contract(contract)
    if contract_failures:
        for failure in contract_failures:
            print(f"production confidence contract error: {failure}", file=sys.stderr)
        return 2
    try:
        current_revision = compute_source_revision(root)
    except RuntimeError as error:
        print(f"production confidence revision error: {error}", file=sys.stderr)
        return 2
    evidence_path = root / contract["evidence"]["path"]
    if not evidence_path.is_file():
        report = make_report(
            contract,
            "missing_evidence",
            ["evidence file does not exist"],
            None,
            current_revision,
        )
        print(json.dumps(report, indent=2, sort_keys=True))
        return 1 if args.require_ready else 0
    try:
        evidence = load_json(evidence_path)
    except (OSError, json.JSONDecodeError) as error:
        failures = [f"evidence is not valid JSON: {error}"]
        revision = None
    else:
        failures, revision = validate_evidence(
            contract, evidence, current_revision
        )
    status = "failed" if failures else "ready"
    print(
        json.dumps(
            make_report(
                contract, status, failures, revision, current_revision
            ),
            indent=2,
            sort_keys=True,
        )
    )
    return 1 if args.require_ready and status != "ready" else 0


if __name__ == "__main__":
    raise SystemExit(main())
