#!/usr/bin/env python3
"""Evaluate the AHFL beta gate from criterion-specific machine evidence."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from ahfl_source_revision import compute_source_revision


DEFAULT_ROOT = Path(__file__).resolve().parents[1]
EXPECTED_CRITERIA = [f"BETA-{index:02d}" for index in range(1, 11)]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT, help="repository root")
    parser.add_argument(
        "--require-ready",
        action="store_true",
        help="return non-zero unless every beta criterion has passing evidence",
    )
    return parser.parse_args()


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def validate_contract(config: Any) -> list[str]:
    failures: list[str] = []
    if not isinstance(config, dict):
        return ["beta gate config must be a JSON object"]
    if config.get("schema") != "ahfl.beta-gate.v1":
        failures.append("beta gate config schema must be ahfl.beta-gate.v1")
    criteria = config.get("criteria")
    if not isinstance(criteria, list):
        return failures + ["beta gate config criteria must be an array"]

    ids = [item.get("id") for item in criteria if isinstance(item, dict)]
    missing = [criterion for criterion in EXPECTED_CRITERIA if criterion not in ids]
    extras = [criterion for criterion in ids if criterion not in EXPECTED_CRITERIA]
    duplicates = sorted({criterion for criterion in ids if ids.count(criterion) > 1})
    if missing:
        failures.append("missing beta criteria: " + ", ".join(missing))
    if extras:
        failures.append("unknown beta criteria: " + ", ".join(str(item) for item in extras))
    if duplicates:
        failures.append("duplicate beta criteria: " + ", ".join(duplicates))

    for item in criteria:
        if not isinstance(item, dict):
            failures.append("each beta criterion must be an object")
            continue
        criterion = str(item.get("id", "<missing>"))
        if not str(item.get("title", "")).strip():
            failures.append(f"{criterion} must define a title")
        evidence = item.get("evidence")
        if not isinstance(evidence, list) or not evidence:
            failures.append(f"{criterion} must define at least one evidence input")
            continue
        for entry in evidence:
            if not isinstance(entry, dict):
                failures.append(f"{criterion} evidence entries must be objects")
                continue
            if not str(entry.get("path", "")).strip():
                failures.append(f"{criterion} evidence path must not be empty")
            if not str(entry.get("schema", "")).strip():
                failures.append(f"{criterion} evidence schema must not be empty")
    return failures


def evaluate_evidence(
    root: Path, criterion: dict[str, Any], entry: dict[str, Any]
) -> dict[str, str]:
    relative_path = str(entry["path"])
    expected_schema = str(entry["schema"])
    path = root / relative_path
    if not path.is_file():
        return {
            "path": relative_path,
            "schema": expected_schema,
            "status": "missing_evidence",
            "message": "evidence file does not exist",
        }
    try:
        evidence = load_json(path)
    except (OSError, json.JSONDecodeError) as error:
        return {
            "path": relative_path,
            "schema": expected_schema,
            "status": "failed",
            "message": f"evidence is not valid JSON: {error}",
        }
    if not isinstance(evidence, dict):
        return {
            "path": relative_path,
            "schema": expected_schema,
            "status": "failed",
            "message": "evidence must be a JSON object",
        }
    if evidence.get("schema") != expected_schema:
        return {
            "path": relative_path,
            "schema": expected_schema,
            "status": "failed",
            "message": f"schema mismatch: found {evidence.get('schema')!r}",
        }
    if evidence.get("criterion") != criterion["id"]:
        return {
            "path": relative_path,
            "schema": expected_schema,
            "status": "failed",
            "message": f"criterion mismatch: found {evidence.get('criterion')!r}",
        }
    source_revision = str(evidence.get("source_revision", "")).strip()
    if not source_revision:
        return {
            "path": relative_path,
            "schema": expected_schema,
            "status": "failed",
            "message": "source_revision must not be empty",
        }
    if evidence.get("status") != "passed":
        return {
            "path": relative_path,
            "schema": expected_schema,
            "status": "failed",
            "message": f"evidence status is {evidence.get('status')!r}, expected 'passed'",
        }
    return {
        "path": relative_path,
        "schema": expected_schema,
        "status": "passed",
        "message": "evidence passed",
        "source_revision": source_revision,
    }


def criterion_status(evidence: list[dict[str, str]]) -> str:
    statuses = {item["status"] for item in evidence}
    if "failed" in statuses:
        return "failed"
    if "missing_evidence" in statuses:
        return "missing_evidence"
    return "passed"


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    config_path = root / "config/beta-gate.json"
    try:
        config = load_json(config_path)
    except (OSError, json.JSONDecodeError) as error:
        print(f"beta gate contract error: {error}", file=sys.stderr)
        return 2

    contract_failures = validate_contract(config)
    if contract_failures:
        for failure in contract_failures:
            print(f"beta gate contract error: {failure}", file=sys.stderr)
        return 2

    criteria_by_id = {criterion["id"]: criterion for criterion in config["criteria"]}
    reports: list[dict[str, Any]] = []
    for criterion_id in EXPECTED_CRITERIA:
        criterion = criteria_by_id[criterion_id]
        evidence = [
            evaluate_evidence(root, criterion, entry) for entry in criterion["evidence"]
        ]
        reports.append(
            {
                "id": criterion_id,
                "title": criterion["title"],
                "status": criterion_status(evidence),
                "evidence": evidence,
            }
        )

    canonical_revision = next(
        (
            evidence["source_revision"]
            for criterion in reports
            for evidence in criterion["evidence"]
            if evidence["status"] == "passed" and "source_revision" in evidence
        ),
        None,
    )
    if canonical_revision is not None:
        for criterion in reports:
            for evidence in criterion["evidence"]:
                source_revision = evidence.get("source_revision")
                if evidence["status"] == "passed" and source_revision != canonical_revision:
                    evidence["status"] = "failed"
                    evidence["message"] = (
                        "source_revision mismatch: "
                        f"found {source_revision!r}, expected {canonical_revision!r}"
                    )
            criterion["status"] = criterion_status(criterion["evidence"])

    try:
        current_source_revision = compute_source_revision(root)
    except RuntimeError as error:
        print(f"beta gate source revision error: {error}", file=sys.stderr)
        return 2
    if canonical_revision is not None and canonical_revision != current_source_revision:
        for criterion in reports:
            for evidence in criterion["evidence"]:
                if evidence["status"] == "passed":
                    evidence["status"] = "failed"
                    evidence["message"] = (
                        "evidence is stale for the current source revision: "
                        f"found {canonical_revision!r}, expected {current_source_revision!r}"
                    )
            criterion["status"] = criterion_status(criterion["evidence"])

    ready = all(criterion["status"] == "passed" for criterion in reports)
    report = {
        "schema": "ahfl.beta-gate-report.v1",
        "rfc": "0012",
        "status": "ready" if ready else "not_ready",
        "source_revision": canonical_revision,
        "current_source_revision": current_source_revision,
        "criteria": reports,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if ready or not args.require_ready else 1


if __name__ == "__main__":
    raise SystemExit(main())
