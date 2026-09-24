#!/usr/bin/env python3
"""RFC 0027 P5 (KR6.12): the CLI query-engine route gate.

Since KR6.11-S4C `ahflc` evaluates the frontend through the self-built query
engine by default. KR6.12 removed the AHFL_QUERY_LEGACY_PIPELINE escape hatch
and the direct resolve/typecheck tail, so there is no second route to
differentiate against: this gate is now the pure non-vacuity gate the P3
migration design (docs/design/query-frontend-p3-migration.zh.md §3.3) says it
degrades to once the legacy half is deleted.

For every corpus entry it runs the command in the DEFAULT environment (with
AHFL_QUERY_ENGINE_TRACE=1) and asserts:

  1. the command exits and is deterministic (rerun stability, including stderr
     after the route line is removed — this catches accidental nondeterminism
     a differential-vs-itself run would also see);
  2. every run that REACHES analysis reports the `query-engine-route: engine`
     trace line on stderr — a regression that stopped routing through the query
     engine would either drop the line or change its token, failing this gate
     rather than hiding behind the golden fleet.

Scope. BOTH input shapes the driver's `run_analysis` template is instantiated
for are covered:

  * bare single files (`run_analysis<ast::Program>`), driven with `check`;
  * package/workspace arrivals (`run_analysis<SourceGraph>`), driven with
    `check` and `emit ir-json` through the integration fixtures that carry an
    `ahfl.toml` / `ahfl.workspace.toml`.

The two corpora are disjoint by construction: the file corpus excludes anything
with a package ancestor, the project corpus is exactly the package fixtures.

Usage:
  query_engine_cli_equiv.py <ahflc> <repo-root> <scratch-dir>
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

# Bare-file commands whose output is deterministic and whose pipeline reaches
# resolve/typecheck (so the engine route has something to serve). `check` is the
# one command a bare single file can drive end to end.
FILE_COMMANDS = (("check",),)

# Package-arrival commands. These require a package context, so they are only run
# against the project corpus below; `emit ir-json` drives the analysis input (the
# engine's graph) all the way through IR lowering and printing.
PROJECT_COMMANDS = (("check",), ("emit", "ir-json"))

# Corpus roots scanned recursively for single-file `.ahfl` sources. Golden
# fixtures include malformed and semantically-erroring sources on purpose.
CORPUS_ROOTS = ("tests/golden", "examples")

# Package fixtures with a workflow target, driven as package arrivals.
PROJECT_FIXTURE_ROOTS = ("tests/integration/package_golden",)

# Upper bound so the gate stays a smoke-sized ctest.
MAX_FILES = 200
MAX_PROJECTS = 40

TRACE_TOKEN = "query-engine-route:"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)


def has_package_ancestor(path: Path, repo_root: Path) -> bool:
    """Whether the file is inside an AHFL package (has an ahfl.toml ancestor)."""
    current = path.parent
    while True:
        if (current / "ahfl.toml").is_file() or (current / "ahfl.workspace.toml").is_file():
            return True
        if current == repo_root or current.parent == current:
            return False
        current = current.parent


def collect_corpus(repo_root: Path) -> list[Path]:
    files: list[Path] = []
    for relative in CORPUS_ROOTS:
        base = repo_root / relative
        if not base.is_dir():
            continue
        files.extend(sorted(p for p in base.rglob("*.ahfl") if p.is_file()))
    files = [p for p in files if not has_package_ancestor(p, repo_root)]
    files.sort()
    return files[:MAX_FILES]


def collect_project_runs(repo_root: Path) -> list[tuple[str, list[str]]]:
    runs: list[tuple[str, list[str]]] = []
    for relative in PROJECT_FIXTURE_ROOTS:
        base = repo_root / relative
        if not base.is_dir():
            continue
        for manifest in sorted(base.rglob("ahfl.toml")):
            text = manifest.read_text(encoding="utf-8", errors="replace")
            if "targets.workflow" not in text:
                continue
            fixture = manifest.relative_to(repo_root)
            for command in PROJECT_COMMANDS:
                args = [
                    *command,
                    "--manifest",
                    str(manifest),
                    "--target",
                    "workflow",
                    "--sysroot",
                    str(repo_root),
                ]
                runs.append((f"{' '.join(command)} {fixture}", args))
    runs.sort(key=lambda entry: entry[0])
    return runs[:MAX_PROJECTS]


def run(ahflc: Path, args: list[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(ahflc), *args],
        capture_output=True,
        text=True,
        env=env,
    )


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    ahflc = Path(sys.argv[1]).resolve()
    repo_root = Path(sys.argv[2]).resolve()
    scratch = Path(sys.argv[3]).resolve()
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)

    corpus = collect_corpus(repo_root)
    if not corpus:
        fail("corpus walk found no .ahfl files (vacuous gate)")
        return 1

    project_runs = collect_project_runs(repo_root)
    if not project_runs:
        fail("project corpus found no package fixtures (vacuous gate)")
        return 1

    # The ONLY route since KR6.12. The trace flag makes the engine route
    # observable. The deleted legacy variable is popped defensively so a stale
    # environment in a developer's shell can never affect this gate (it is now
    # an unknown variable the driver ignores).
    env = dict(os.environ)
    env.pop("AHFL_QUERY_ENGINE", None)
    env.pop("AHFL_QUERY_LEGACY_PIPELINE", None)
    env["AHFL_QUERY_ENGINE_TRACE"] = "1"

    def route_of(stderr: str) -> str | None:
        for line in stderr.splitlines():
            if line.startswith(TRACE_TOKEN):
                return line.split(":", 1)[1].strip()
        return None

    def without_route(text: str) -> str:
        return "\n".join(
            line for line in text.splitlines() if not line.startswith(TRACE_TOKEN)
        )

    compared = 0
    analysis_reached = 0
    failures = 0

    file_runs = [
        (f"{' '.join(command)} {path.relative_to(repo_root)}", [*command, str(path)])
        for path in corpus
        for command in FILE_COMMANDS
    ]
    file_compared = len(file_runs)
    project_compared = len(project_runs)

    for label, args in file_runs + project_runs:
        first = run(ahflc, args, env)
        second = run(ahflc, args, env)

        # Determinism: the query route must produce identical output across
        # reruns in the same environment (the route line stripped).
        if first.returncode != second.returncode:
            fail(f"{label}: exit code differs across reruns")
            failures += 1
            continue
        if first.stdout != second.stdout:
            fail(f"{label}: stdout differs across reruns")
            failures += 1
            continue
        if without_route(first.stderr) != without_route(second.stderr):
            fail(f"{label}: stderr differs across reruns (besides the route line)")
            failures += 1
            continue

        route = route_of(first.stderr)
        if route is not None:
            # Analysis was reached: it MUST have gone through the query engine.
            if route != "engine":
                fail(f"{label}: analysis did not engage the engine route ({route!r})")
                failures += 1
                continue
            analysis_reached += 1
        compared += 1

    if compared == 0:
        fail("no command/files were compared (vacuous gate)")
        return 1
    if analysis_reached == 0:
        fail("no run reached analysis (vacuous gate)")
        return 1

    print(
        f"query-engine default-route gate: {compared} runs "
        f"({file_compared} file + {project_compared} project), "
        f"{analysis_reached} reached analysis via the engine"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
