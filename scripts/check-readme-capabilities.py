#!/usr/bin/env python3
"""Verify that root README capability claims are backed by beta evidence."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from ahfl_source_revision import compute_source_revision


MARKER = re.compile(
    r"<!-- beta-capability:(BETA-\d{2}) schema=([a-zA-Z0-9._-]+) -->"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--out", type=Path)
    return parser.parse_args()


def fail(message: str) -> None:
    print(f"README capability gate failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def load_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"failed to read {path}: {error}")
    if not isinstance(value, dict):
        fail(f"{path} must contain a JSON object")
    return value


def marker_map(path: Path, text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for criterion, schema in MARKER.findall(text):
        if criterion in result:
            fail(f"{path} contains duplicate marker {criterion}")
        result[criterion] = schema
    return result


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    contract = load_json(root / "config/readme-capabilities.json")
    if contract.get("schema") != "ahfl.readme-capabilities.v1":
        fail("unexpected capability contract schema")
    gate = load_json(root / "config/beta-gate.json")
    criteria = {
        criterion["id"]: criterion
        for criterion in gate.get("criteria", [])
        if isinstance(criterion, dict) and isinstance(criterion.get("id"), str)
    }

    readmes = {
        "english": (root / "README.md").read_text(encoding="utf-8"),
        "chinese": (root / "README.zh.md").read_text(encoding="utf-8"),
    }
    markers = {
        language: marker_map(root / ("README.md" if language == "english" else "README.zh.md"), text)
        for language, text in readmes.items()
    }

    claims = contract.get("claims")
    if not isinstance(claims, list) or not claims:
        fail("capability contract must define claims")
    expected_ids: list[str] = []
    evidence_schemas: dict[str, str] = {}
    for claim in claims:
        if not isinstance(claim, dict):
            fail("every capability claim must be an object")
        criterion = claim.get("id")
        schema = claim.get("evidence_schema")
        if not isinstance(criterion, str) or not isinstance(schema, str):
            fail("capability claim id and evidence_schema must be strings")
        if criterion in expected_ids:
            fail(f"duplicate capability contract entry {criterion}")
        expected_ids.append(criterion)
        evidence_schemas[criterion] = schema

        gate_criterion = criteria.get(criterion)
        if gate_criterion is None:
            fail(f"capability contract references unknown criterion {criterion}")
        evidence_entries = gate_criterion.get("evidence", [])
        if not isinstance(evidence_entries, list) or len(evidence_entries) != 1:
            fail(f"{criterion} must have exactly one evidence entry")
        evidence_entry = evidence_entries[0]
        if evidence_entry.get("schema") != schema:
            fail(f"{criterion} evidence schema disagrees with beta gate")
        evidence_path = root / evidence_entry["path"]
        evidence = load_json(evidence_path)
        if (
            evidence.get("schema") != schema
            or evidence.get("criterion") != criterion
            or evidence.get("status") != "passed"
        ):
            fail(f"{criterion} evidence is missing, failed, or schema-incompatible")

        for language in ("english", "chinese"):
            text = claim.get(language)
            if not isinstance(text, str) or not text:
                fail(f"{criterion} is missing {language} claim text")
            if text not in readmes[language]:
                fail(f"{criterion} {language} README claim does not match the contract")
            if markers[language].get(criterion) != schema:
                fail(f"{criterion} {language} README marker is missing or has the wrong schema")

    expected = set(expected_ids)
    for language, found in markers.items():
        missing = expected - set(found)
        extra = set(found) - expected
        if missing:
            fail(f"{language} README is missing markers: {', '.join(sorted(missing))}")
        if extra:
            fail(f"{language} README has ungoverned markers: {', '.join(sorted(extra))}")

    for pattern in contract.get("forbidden_claim_patterns", []):
        if not isinstance(pattern, str):
            fail("forbidden claim patterns must be strings")
        compiled = re.compile(pattern, re.IGNORECASE | re.DOTALL)
        for language, text in readmes.items():
            match = compiled.search(text)
            if match is not None:
                fail(f"{language} README contains unverified claim '{match.group(0)}'")

    if args.out is not None:
        output = args.out.resolve()
        value: dict[str, object] = {
            "schema": "ahfl.beta-evidence.readme-capabilities.v1",
            "status": "passed",
            "criterion": "BETA-09",
            "source_revision": compute_source_revision(root),
            "readmes": ["README.md", "README.zh.md"],
            "covered_criteria": expected_ids,
            "evidence_schemas": evidence_schemas,
            "language_parity": True,
            "ungoverned_claims": [],
        }
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print("README capability gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
