#!/usr/bin/env python3
"""KR2.7: SMV-size trend gate with a committed baseline and release-blocking
thresholds.

Unlike RunSmvSizeBudgetTest.cmake (fixed MAX/MIN per fixture), this gate records
a *baseline* of emit-smv sizes and fails when any fixture's size regresses beyond
a relative tolerance — turning the budget from a static ceiling into a trend
guard. It also writes a machine-readable trend report (`ahfl.smv-size-trend.v2`)
with per-fixture baseline/current/delta. The report is a local, per-run
diagnostic written to the build tree: it is not uploaded as a CI artifact, so a
release process cannot chart history from it.

Fixture set (KR7.3): the baseline tracks the full representative set of five
fixtures — the two formal-semantics fixtures plus the three productization
fixtures that the static `ahflc.quality.smv_size_budget.*` tests already budget
(pass-productization, workflow-simplification, refund-audit). The fixture set is
*required* to keep that breadth: the gate refuses to pass unless every fixture
named in REQUIRED_FIXTURES is present in the baseline, and it blocks when a
required fixture's entry is missing `metrics` or (deliberately) `pass_order`.
Adding a fixture is otherwise mechanical — add its `source` and run `--update`
to seed the measured `metrics` and `pass_order`; the ctest registration reads
the baseline generically, so the fixture list has exactly one source of truth.

Pass-order determinism: a fixture entry may declare `pass_order`, the exact
sequence of optimization pass names the `-O` pipeline runs for that source. This
is measured from the same `emit smv` pipeline the sizes come from, run with `-O`
and `--pass-trace-export`, and is guarded exactly like the size metrics: any
reordering or dropped/added pass fails the gate. Note the size metrics are
deliberately measured *without* `-O` (the emit-smv output contract), while
`pass_order` is measured *with* `-O`; the two are separate contracts and `-O`
changes the emitted SMV bytes for some fixtures.

Usage:
  smv_size_trend_gate.py <ahflc> <baseline.json> <report-out.json> [--update]

`--update` rewrites the baseline from the current measurements (for intentional,
reviewed size changes) instead of enforcing it. It rewrites the metrics of every
entry and the `pass_order` of every entry that declares one, so it seeds a
newly added fixture (which need only supply `source`) rather than aborting on it.
Coverage is never traded away: `--update` refuses to pass if a REQUIRED_FIXTURES
entry has been dropped from the baseline.
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

# Relative regression tolerance: a fixture may grow up to this fraction over its
# baseline before the gate blocks. Byte/line/ltlspec each guarded. Shrinking is
# always allowed (and, for ltlspec, a drop below baseline is also blocked since
# it means specs were silently lost).
GROWTH_TOLERANCE = 0.10

# Fixtures the baseline is required to track (the "full 5 representative
# fixtures" guarantee). This is the single source of truth for the *minimum*
# fixture set: the gate blocks when one is missing, so coverage cannot be
# silently deleted, and each is also required to carry guarded `metrics` and a
# pinned `-O` `pass_order`. Deleting a fixture is therefore a source change that
# trips the gate and must be a deliberate, reviewed edit here as well.
REQUIRED_FIXTURES = (
    "bounded_data_semantics",
    "flow_workflow_semantics",
    "pass_productization",
    "refund_audit",
    "workflow_simplification",
)

# Committed baseline schema. Adding the optional per-fixture `pass_order` field
# was additive (a consumer that ignores it parses the document unchanged), so the
# version string is unchanged (still v1) and is reserved for a real shape break.
BASELINE_SCHEMA = "ahfl.smv-size-baseline.v1"
# Trend report schema. Unlike the baseline this genuinely gained per-fixture
# baseline_pass_order/current_pass_order fields, so it is v2.
TREND_SCHEMA = "ahfl.smv-size-trend.v2"


def measure(ahflc: Path, source: Path) -> dict[str, int]:
    result = subprocess.run(
        [str(ahflc), "emit", "smv", str(source)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"emit smv failed for {source}:\n{result.stderr}")
    text = result.stdout
    return {
        "bytes": len(text.encode("utf-8")),
        "lines": text.count("\n") + (0 if text.endswith("\n") or not text else 1),
        "ltlspec": sum(1 for ln in text.splitlines() if ln.startswith("LTLSPEC")),
    }


def measure_pass_order(ahflc: Path, source: Path) -> list[str]:
    """Return the `-O` optimization pass sequence for `source`.

    Runs the same `emit smv` command the size metrics use, plus `-O` and the
    documented `--pass-trace-export` (the surface exercised by
    tests/scripts/pass_trace_gate.py). The trace is written to a scratch file
    outside the tree, so this gate never depends on stdout.
    """
    with tempfile.TemporaryDirectory() as scratch:
        trace = Path(scratch) / "pass_trace.json"
        result = subprocess.run(
            [
                str(ahflc),
                "emit",
                "smv",
                "-O",
                "--pass-trace-export",
                str(trace),
                str(source),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"emit smv -O pass trace failed for {source}:\n{result.stderr}"
            )
        doc = json.loads(trace.read_text(encoding="utf-8"))
    passes = doc.get("passes")
    if not isinstance(passes, list) or not passes:
        raise RuntimeError(f"pass trace for {source} recorded no passes")
    return [record["name"] for record in passes]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("ahflc", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("report_out", type=Path)
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()

    baseline_doc = json.loads(args.baseline.read_text(encoding="utf-8"))
    if baseline_doc.get("schema") != BASELINE_SCHEMA:
        print(f"baseline schema must be {BASELINE_SCHEMA}", file=sys.stderr)
        return 2
    # baseline lives at <repo>/config/smv-size-baseline.json → repo root is its
    # grandparent. Fixture sources are repo-root-relative.
    repo_root = args.baseline.resolve().parents[1]

    fixtures = baseline_doc["fixtures"]
    entries: list[dict[str, object]] = []
    failures: list[str] = []

    # Coverage floor: the fixture set is a committed decision, not whatever the
    # file happens to contain. A required fixture dropped from the baseline
    # (or a wholesale emptied map) is a coverage regression that must fail, not
    # a smaller-but-greener run.
    missing_required = [name for name in REQUIRED_FIXTURES if name not in fixtures]
    if missing_required:
        failures.append(
            f"baseline is missing required fixtures {missing_required} — coverage "
            f"was removed; restore them (the set is a reviewed decision)"
        )

    for name, spec in sorted(fixtures.items()):
        source = repo_root / spec["source"]
        current = measure(args.ahflc, source)
        # A fixture may be added with only `source`; `--update` seeds metrics.
        # Absent metrics in enforce mode is a diagnostic, not a traceback.
        base = spec.get("metrics")
        if not isinstance(base, dict):
            if args.update:
                spec["metrics"] = current
                base = current
            elif name in REQUIRED_FIXTURES:
                failures.append(
                    f"{name}: required fixture has no `metrics` — the baseline entry "
                    f"is incomplete; run with --update to seed it"
                )
                continue
            else:
                print(
                    f"{name}: no baseline metrics; run with --update to seed them",
                    file=sys.stderr,
                )
                return 2
        deltas = {k: current[k] - base[k] for k in current}
        entry = {
            "fixture": name,
            "source": spec["source"],
            "baseline": base,
            "current": current,
            "delta": deltas,
        }
        entries.append(entry)

        if not args.update:
            for metric in ("bytes", "lines"):
                limit = int(base[metric] * (1.0 + GROWTH_TOLERANCE))
                if current[metric] > limit:
                    failures.append(
                        f"{name}.{metric} regressed: {current[metric]} > baseline "
                        f"{base[metric]} +{int(GROWTH_TOLERANCE * 100)}% ({limit})"
                    )
            # LTLSPEC count must not drop (silently lost specs) nor balloon.
            if current["ltlspec"] < base["ltlspec"]:
                failures.append(
                    f"{name}.ltlspec dropped: {current['ltlspec']} < baseline "
                    f"{base['ltlspec']} — a spec was lost"
                )
            elif current["ltlspec"] > base["ltlspec"]:
                failures.append(
                    f"{name}.ltlspec grew: {current['ltlspec']} > baseline "
                    f"{base['ltlspec']}"
                )

        # Pass order is guarded for every REQUIRED_FIXTURES entry (breadth is a
        # committed decision) and stays opt-in for any extra fixture that does
        # not declare it, so the field is not a second implicit fixture list.
        if "pass_order" in spec or name in REQUIRED_FIXTURES:
            current_order = measure_pass_order(args.ahflc, source)
            entry["current_pass_order"] = current_order
            if "pass_order" in spec:
                entry["baseline_pass_order"] = spec["pass_order"]
                if not args.update and current_order != spec["pass_order"]:
                    failures.append(
                        f"{name}.pass_order changed: {current_order} != baseline "
                        f"{spec['pass_order']}"
                    )
            elif not args.update:
                failures.append(
                    f"{name}: required fixture pins no `pass_order` — the -O pass "
                    f"sequence is unguarded; run with --update to seed it"
                )

    report = {
        "schema": TREND_SCHEMA,
        "tolerance": GROWTH_TOLERANCE,
        "status": "updated" if args.update else ("failed" if failures else "passed"),
        "fixtures": entries,
        "failures": failures,
    }
    args.report_out.parent.mkdir(parents=True, exist_ok=True)
    args.report_out.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    if args.update:
        # A coverage regression is never a legitimate update: refuse to write.
        if failures:
            print("baseline update refused:", file=sys.stderr)
            for f in failures:
                print(f"  {f}", file=sys.stderr)
            return 1
        for entry in entries:
            fixture = fixtures[entry["fixture"]]
            fixture["metrics"] = entry["current"]
            if "current_pass_order" in entry:
                fixture["pass_order"] = entry["current_pass_order"]
        args.baseline.write_text(
            json.dumps(baseline_doc, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"baseline updated: {args.baseline}")
        return 0

    if failures:
        print("SMV size trend gate failed:", file=sys.stderr)
        for f in failures:
            print(f"  {f}", file=sys.stderr)
        return 1
    print(f"SMV size trend gate passed ({len(entries)} fixtures within tolerance)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
