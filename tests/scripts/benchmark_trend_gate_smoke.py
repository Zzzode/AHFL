#!/usr/bin/env python3
"""Regression smoke test for the KR7.3 benchmark trend gate's contracts.

`benchmark_trend_gate.py` is a release gate, so its *own* behaviour under the
conditions it exists to catch must be pinned. The gate drives two bench binaries
through their `ahfl.bench-report.v1` report, so this test substitutes tiny stub
"binaries" (shell scripts writing a fixed report) and exercises the contract the
gate promises:

  * enforce mode passes when both benches agree with the baseline,
  * structural drift fails,
  * `--update` is exempt from the *tolerance* checks and rewrites the baseline,
  * `--update` is NOT exempt from the gate's *invariants*: a cross-bench
    disagreement, or a budget measured by only one bench, is refused with the
    baseline left byte-identical (the finding this test pins),
  * a malformed baseline entry (a guarded metric missing) reports the budget and
    key and returns 2, instead of an uncaught KeyError traceback.

Uses only the standard library and `python3`; no bench binary is built.
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

GATE = Path(__file__).resolve().with_name("benchmark_trend_gate.py")
METRICS = ("source_bytes", "typed_exprs", "typed_statements", "ir_decls", "ir_exprs",
           "proxy_bytes")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def make_stub(path: Path, kind: str, budgets: dict[str, dict[str, int]]) -> None:
    """Write a shell 'bench binary' that emits `budgets` as a bench report."""
    rows = []
    for name, metrics in budgets.items():
        fields = ", ".join(f'"{k}": {v}' for k, v in {**metrics, "duration_us": 1}.items())
        rows.append(f'    {{"name": "{name}", {fields}}}')
    document = (
        "{\n"
        f'  "schema": "ahfl.bench-report.v1",\n'
        f'  "kind": "{kind}",\n'
        '  "budgets": [\n' + ",\n".join(rows) + "\n  ]\n}\n"
    )
    body = f'#!/bin/sh\ncat > "$2" <<\'AHFL_EOF\'\n{document}AHFL_EOF\n'
    path.write_text(body, encoding="utf-8")
    path.chmod(0o755)


def write_baseline(path: Path, budgets: dict[str, dict[str, int]]) -> None:
    path.write_text(
        json.dumps(
            {"schema": "ahfl.benchmark-baseline.v1",
             "budgets": {n: {"metrics": m} for n, m in budgets.items()}},
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


def run_gate(ct: Path, mu: Path, baseline: Path, report: Path, *extra: str):
    return subprocess.run(
        [sys.executable, str(GATE), str(ct), str(mu), str(baseline), str(report), *extra],
        check=False,
        capture_output=True,
        text=True,
    )


def metrics(**overrides: int) -> dict[str, int]:
    base = {"source_bytes": 100, "typed_exprs": 10, "typed_statements": 9,
            "ir_decls": 2, "ir_exprs": 10, "proxy_bytes": 5000}
    base.update(overrides)
    return base


def main() -> int:
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        baseline = root / "baseline.json"
        report = root / "trend.json"
        ct = root / "ct.sh"
        mu = root / "mu.sh"

        # 1. Agreeing benches within baseline: pass.
        write_baseline(baseline, {"small": metrics()})
        make_stub(ct, "compile_time", {"small": metrics()})
        make_stub(mu, "memory_usage", {"small": metrics()})
        result = run_gate(ct, mu, baseline, report)
        require(result.returncode == 0, f"clean run must pass: {result.stdout}{result.stderr}")
        require("passed" in result.stdout, f"expected a pass message: {result.stdout}")

        # 2. Structural drift fails.
        make_stub(ct, "compile_time", {"small": metrics(typed_exprs=11)})
        make_stub(mu, "memory_usage", {"small": metrics(typed_exprs=11)})
        result = run_gate(ct, mu, baseline, report)
        require(result.returncode == 1, "drift must fail")
        require("drifted" in result.stderr, f"expected a drift diagnostic: {result.stderr}")

        # 3. --update resets a reviewed change (tolerance check) and rewrites.
        result = run_gate(ct, mu, baseline, report, "--update")
        require(result.returncode == 0, f"reviewed --update must succeed: {result.stderr}")
        updated = json.loads(baseline.read_text(encoding="utf-8"))
        require(updated["budgets"]["small"]["metrics"]["typed_exprs"] == 11,
                "reviewed --update must rewrite the baseline")

        # 4. Cross-bench disagreement: --update is refused and the baseline is
        #    untouched (the persisted-drift hole this gate must not have).
        write_baseline(baseline, {"small": metrics()})
        before = baseline.read_bytes()
        make_stub(ct, "compile_time", {"small": metrics(typed_exprs=42)})
        make_stub(mu, "memory_usage", {"small": metrics(typed_exprs=10)})
        result = run_gate(ct, mu, baseline, report, "--update")
        require(result.returncode == 1, "disagreement must refuse --update")
        require("refused" in result.stderr, f"expected a refusal message: {result.stderr}")
        require(baseline.read_bytes() == before,
                "refused --update must leave the baseline byte-identical")

        # 5. One-sided measurement: --update is refused, baseline untouched.
        before = baseline.read_bytes()
        make_stub(ct, "compile_time", {"small": metrics(), "ghost": metrics()})
        make_stub(mu, "memory_usage", {"small": metrics()})
        result = run_gate(ct, mu, baseline, report, "--update")
        require(result.returncode == 1, "one-sided budget must refuse --update")
        require("measured only by" in result.stderr,
                f"expected a one-sided diagnostic: {result.stderr}")
        require(baseline.read_bytes() == before,
                "refused --update must leave the baseline byte-identical")

        # 6. Malformed baseline: a missing guarded metric is code 2 with a
        #    diagnostic, not a KeyError traceback.
        make_stub(ct, "compile_time", {"small": metrics()})
        make_stub(mu, "memory_usage", {"small": metrics()})
        malformed = root / "malformed.json"
        write_baseline(malformed, {"small": metrics()})
        loaded = json.loads(malformed.read_text(encoding="utf-8"))
        del loaded["budgets"]["small"]["metrics"]["proxy_bytes"]
        malformed.write_text(json.dumps(loaded, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")
        result = run_gate(ct, mu, malformed, report)
        require(result.returncode == 2, f"malformed baseline must return 2: {result.stderr}")
        require("proxy_bytes" in result.stderr,
                f"diagnostic must name the missing metric: {result.stderr}")
        require("Traceback" not in result.stderr,
                f"malformed baseline must not traceback: {result.stderr}")

    print("benchmark trend gate smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
