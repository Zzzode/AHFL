#!/usr/bin/env python3
"""KR2.7: SMV-size trend gate with a committed baseline and release-blocking
thresholds.

Unlike RunSmvSizeBudgetTest.cmake (fixed MAX/MIN per fixture), this gate records
a *baseline* of emit-smv sizes and fails when any fixture's size regresses beyond
a relative tolerance — turning the budget from a static ceiling into a trend
guard. It also writes a machine-readable trend report artifact
(`ahfl.smv-size-trend.v1`) with per-fixture baseline/current/delta so a release
process can chart drift over time.

Usage:
  smv_size_trend_gate.py <ahflc> <baseline.json> <report-out.json> [--update]

`--update` rewrites the baseline from the current measurements (for intentional,
reviewed size changes) instead of enforcing it.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

# Relative regression tolerance: a fixture may grow up to this fraction over its
# baseline before the gate blocks. Byte/line/ltlspec each guarded. Shrinking is
# always allowed (and, for ltlspec, a drop below baseline is also blocked since
# it means specs were silently lost).
GROWTH_TOLERANCE = 0.10


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("ahflc", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("report_out", type=Path)
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()

    baseline_doc = json.loads(args.baseline.read_text(encoding="utf-8"))
    if baseline_doc.get("schema") != "ahfl.smv-size-baseline.v1":
        print("baseline schema must be ahfl.smv-size-baseline.v1", file=sys.stderr)
        return 2
    # baseline lives at <repo>/config/smv-size-baseline.json → repo root is its
    # grandparent. Fixture sources are repo-root-relative.
    repo_root = args.baseline.resolve().parents[1]

    fixtures = baseline_doc["fixtures"]
    entries: list[dict[str, object]] = []
    failures: list[str] = []

    for name, spec in sorted(fixtures.items()):
        source = repo_root / spec["source"]
        current = measure(args.ahflc, source)
        base = spec["metrics"]
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

    report = {
        "schema": "ahfl.smv-size-trend.v1",
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
        for entry in entries:
            fixtures[entry["fixture"]]["metrics"] = entry["current"]
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
