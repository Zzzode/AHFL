#!/usr/bin/env python3
"""KR7.3: compile-time / memory-proxy trend gate with a committed baseline.

`ahfl_bench_compile_time` and `ahfl_bench_memory_usage` are static-ceiling gates:
they fail when a single budget is breached, but a ceiling with headroom admits a
long slow slide that never trips it, and neither binary produced an artifact, so
no drift could be charted. This gate closes that gap the same way the accepted
SMV-size trend gate does (`smv_size_trend_gate.py`), reusing its shape:

  * run each bench binary with the opt-in machine-readable report
    (`--report <scratch>`, schema `ahfl.bench-report.v1`),
  * compare every measured budget against the committed baseline
    (`config/benchmark-baseline.json`, schema `ahfl.benchmark-baseline.v1`),
  * fail when a structural metric drifts at all, or when the memory proxy grows
    past a relative tolerance, and
  * write a machine-readable trend artifact (`ahfl.benchmark-trend.v1`) with
    per-budget baseline/current/delta for a release process to chart.

Both benches compile the *same* fixture ladder and use the one shared
`make_bench_metric` / `estimate_proxy_bytes` definition, so a budget name's
structural metrics must be identical in the two reports. The gate asserts that
cross-bench agreement directly (it is the observable form of "the metric
definition has a single source of truth"); a disagreement fails rather than being
silently averaged.

Guard policy (why each metric is guarded the way it is):

  * `source_bytes`, `typed_exprs`, `typed_statements`, `ir_decls`, `ir_exprs`
    are *structural* and fully deterministic for a fixed lowering, so ANY change
    is a real change in what the compiler builds. Guarded exactly.
  * `proxy_bytes` is the shared memory proxy (`estimate_proxy_bytes`). It scales
    with the structural counts, so it is guarded with a relative tolerance rather
    than exactly — it catches a genuine per-node cost regression without failing
    on a benign, intentional count change the exact guards already reviewed.
  * `duration_us` is wall-clock and machine-dependent: the report records it, but
    it is never stored in the baseline and never blocks. The binaries' own static
    `max_duration` ceilings remain the absolute wall-clock bound; this gate must
    not be flaky on a loaded CI box.

Usage:
  benchmark_trend_gate.py <compile_time-bin> <memory_usage-bin> \\
      <baseline.json> <report-out.json> [--update]

`--update` rewrites the baseline's metrics from the current measurements (for
intentional, reviewed changes) instead of enforcing them. It rewrites only the
budgets the baseline already declares and refuses to add or drop one, so the
baseline's budget set stays a reviewed decision.
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

# Report artifacts this gate produces/consumes.
BENCH_REPORT_SCHEMA = "ahfl.bench-report.v1"
BASELINE_SCHEMA = "ahfl.benchmark-baseline.v1"
TREND_SCHEMA = "ahfl.benchmark-trend.v1"

# Metrics guarded exactly (any drift fails). Structural + deterministic.
EXACT_METRICS = ("source_bytes", "typed_exprs", "typed_statements", "ir_decls", "ir_exprs")
# Memory proxy: relative growth tolerance before the gate blocks.
PROXY_METRIC = "proxy_bytes"
PROXY_GROWTH_TOLERANCE = 0.10
# Report-only: recorded with a delta but never stored nor blocking.
REPORT_ONLY_METRICS = ("duration_us",)

# (baseline budget name, binary kind). One row per bench, so the bench set has a
# single source of truth.
BENCHES = ("compile_time", "memory_usage")

GUARDED_METRICS = (*EXACT_METRICS, PROXY_METRIC)


def run_bench(binary: Path, kind: str) -> list[dict[str, object]]:
    """Run one bench binary with a scratch report path and return its rows."""
    with tempfile.TemporaryDirectory() as scratch:
        report = Path(scratch) / "bench-report.json"
        result = subprocess.run(
            [str(binary), "--report", str(report)],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"{binary.name} failed (exit {result.returncode}); its static budget "
                f"gate must pass before the trend can be judged:\n{result.stdout}\n{result.stderr}"
            )
        doc = json.loads(report.read_text(encoding="utf-8"))

    if doc.get("schema") != BENCH_REPORT_SCHEMA:
        raise RuntimeError(
            f"{binary.name} report schema {doc.get('schema')!r} != {BENCH_REPORT_SCHEMA!r}"
        )
    if doc.get("kind") != kind:
        raise RuntimeError(f"{binary.name} report kind {doc.get('kind')!r} != {kind!r}")
    budgets = doc.get("budgets")
    if not isinstance(budgets, list) or not budgets:
        raise RuntimeError(f"{binary.name} report recorded no budgets")
    return budgets


def metrics_of(row: dict[str, object], fields: tuple[str, ...]) -> dict[str, int]:
    missing = [metric for metric in fields if metric not in row]
    if missing:
        raise RuntimeError(f"budget {row.get('name')!r} is missing metrics {missing}")
    return {metric: int(row[metric]) for metric in fields if metric in row}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("compile_time_bin", type=Path)
    parser.add_argument("memory_usage_bin", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("report_out", type=Path)
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()

    baseline_doc = json.loads(args.baseline.read_text(encoding="utf-8"))
    if baseline_doc.get("schema") != BASELINE_SCHEMA:
        print(f"baseline schema must be {BASELINE_SCHEMA}", file=sys.stderr)
        return 2

    binaries = {
        "compile_time": args.compile_time_bin,
        "memory_usage": args.memory_usage_bin,
    }
    baseline_budgets = baseline_doc["budgets"]

    # name -> {kind -> {guarded metrics}}; the two benches must agree.
    measured: dict[str, dict[str, dict[str, int]]] = {}
    durations: dict[str, dict[str, int]] = {}
    for kind in BENCHES:
        for row in run_bench(binaries[kind], kind):
            name = str(row["name"])
            measured.setdefault(name, {})[kind] = metrics_of(row, GUARDED_METRICS)
            durations.setdefault(name, {})[kind] = metrics_of(row, REPORT_ONLY_METRICS)[
                REPORT_ONLY_METRICS[0]
            ]

    entries: list[dict[str, object]] = []
    failures: list[str] = []
    warnings: list[str] = []

    seen = set(measured)
    for name in sorted(seen):
        by_kind = measured[name]
        missing = [kind for kind in BENCHES if kind not in by_kind]
        if missing:
            failures.append(
                f"{name}: measured only by {sorted(by_kind)} — a budget present in one "
                f"bench must be present in both (missing {missing})"
            )
            continue

        # Cross-bench agreement: the metric definition is shared, so the two
        # benches must report the same guarded numbers for the same budget.
        reference_kind = BENCHES[0]
        current = by_kind[reference_kind]
        for kind in BENCHES[1:]:
            if by_kind[kind] != current:
                failures.append(
                    f"{name}: {kind} and {reference_kind} disagree — "
                    f"{by_kind[kind]} != {current}; the shared bench metric "
                    f"definition (`make_bench_metric`) is not being used consistently"
                )

        entry: dict[str, object] = {
            "name": name,
            "current": {
                **current,
                REPORT_ONLY_METRICS[0]: {kind: durations[name][kind] for kind in BENCHES},
            },
        }

        if name not in baseline_budgets:
            failures.append(
                f"{name}: no baseline entry — add it to {args.baseline.name} "
                f"(reviewed) to track this budget"
            )
            entries.append(entry)
            continue

        base = baseline_budgets[name]["metrics"]
        deltas = {metric: current[metric] - base[metric] for metric in GUARDED_METRICS}
        entry["baseline"] = base
        entry["delta"] = deltas
        entries.append(entry)

        if args.update:
            continue

        for metric in EXACT_METRICS:
            if current[metric] != base[metric]:
                failures.append(
                    f"{name}.{metric} drifted: {current[metric]} != baseline {base[metric]} "
                    f"(delta {deltas[metric]:+d})"
                )

        limit = int(base[PROXY_METRIC] * (1.0 + PROXY_GROWTH_TOLERANCE))
        if current[PROXY_METRIC] > limit:
            failures.append(
                f"{name}.{PROXY_METRIC} regressed: {current[PROXY_METRIC]} > baseline "
                f"{base[PROXY_METRIC]} +{int(PROXY_GROWTH_TOLERANCE * 100)}% ({limit})"
            )

    for name in sorted(set(baseline_budgets) - seen):
        failures.append(
            f"{name}: baseline entry has no measured budget — a budget was renamed or "
            f"removed; update {args.baseline.name} (reviewed)"
        )

    for name in sorted(seen):
        for kind in BENCHES:
            if kind in durations[name]:
                warnings.append(
                    f"{name}.{REPORT_ONLY_METRICS[0]}[{kind}] = {durations[name][kind]} "
                    f"(report-only, machine-dependent)"
                )

    report = {
        "schema": TREND_SCHEMA,
        "status": "updated" if args.update else ("failed" if failures else "passed"),
        "proxy_tolerance": PROXY_GROWTH_TOLERANCE,
        "benches": list(BENCHES),
        "budgets": entries,
        "failures": failures,
        "warnings": warnings,
    }
    args.report_out.parent.mkdir(parents=True, exist_ok=True)
    args.report_out.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    if args.update:
        for entry in entries:
            name = entry["name"]
            if name in baseline_budgets:
                baseline_budgets[name]["metrics"] = {
                    metric: entry["current"][metric] for metric in GUARDED_METRICS
                }
        args.baseline.write_text(
            json.dumps(baseline_doc, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"baseline updated: {args.baseline}")
        return 0

    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)
    if failures:
        print("Benchmark trend gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(f"Benchmark trend gate passed ({len(entries)} budgets within tolerance)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
