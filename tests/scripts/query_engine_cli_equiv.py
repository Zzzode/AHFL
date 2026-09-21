#!/usr/bin/env python3
"""RFC 0027 P3 (KR6.11-S4): the CLI query-engine route equivalence gate.

`ahflc` can evaluate the frontend through the self-built query engine instead of
the direct pipeline, under `AHFL_QUERY_ENGINE=1` (see `query_engine_route_requested`
in `src/tooling/cli/cli_driver.cpp`). This gate is the "exhaustive equivalence
ctest" the S4 slice requires: for a corpus of sources it runs the same command
twice — once on each route — and asserts the exit code, stdout, and stderr are
byte-identical. stdout is the artifact a user sees (IR JSON, SMV, summaries), so
byte-equality here is the CLI-level statement of RFC 0027's migration criterion
("the query result is the same result").

Non-vacuity. An equivalence gate that silently fell back to the direct pipeline on
every input would pass trivially. The engine route emits a
`query-engine-route:` trace line on stderr *only* when it actually served the
analysis; with `AHFL_QUERY_ENGINE_TRACE=1` this gate asserts that every corpus
entry produced the trace, so a regression that disables the route fails the gate
rather than hiding behind byte-equality.

Scope. BOTH input shapes the driver's `run_analysis` template is instantiated for
are covered, so the gate spans the file/package arrival boundary the driver's own
comment describes:

  * bare single files (`run_analysis<ast::Program>`), driven with `check`;
  * package/workspace arrivals (`run_analysis<SourceGraph>`), driven with
    `check` and `emit ir-json` through the integration fixtures that carry an
    `ahfl.toml` / `ahfl.workspace.toml`. A `SourceGraph` became routable once its
    parse was expressed as a value-semantics `ProjectInputModel`; before that the
    package path was deliberately unrouted, which is why this file previously
    filtered package files out.

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
# one command a bare single file can drive end to end: `emit ir` / `emit ir-json`
# refuse a bare file with a usage error (they require a package manifest), so they
# never reach `run_analysis` and are not part of the bare-file half of this gate.
FILE_COMMANDS = (("check",),)

# Package-arrival commands. These require a package context, so they are only run
# against the project corpus below; `emit ir-json` is included because it drives
# the analysis input (the engine's graph) all the way through IR lowering and
# printing, which is the strongest artifact-level statement of equivalence.
PROJECT_COMMANDS = (("check",), ("emit", "ir-json"))

# Corpus roots scanned recursively for single-file `.ahfl` sources. Golden
# fixtures include malformed and semantically-erroring sources on purpose: route
# equivalence must hold for the failure projections too, not just success.
CORPUS_ROOTS = ("tests/golden", "examples")

# Package fixtures with a workflow target, driven as package arrivals. Each entry
# is a (manifest-or-workspace path, extra args) pair; the target and sysroot are
# appended uniformly below so the package graph build is identical to the one the
# CLI golden fleet uses.
PROJECT_FIXTURE_ROOTS = ("tests/integration/package_golden",)

# Upper bound so the gate stays a smoke-sized ctest. The corpus is walked in
# sorted order, which is deterministic run to run.
MAX_FILES = 200
MAX_PROJECTS = 40

TRACE_TOKEN = "query-engine-route:"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)


def has_package_ancestor(path: Path, repo_root: Path) -> bool:
    """Whether the file is inside an AHFL package (has an ahfl.toml ancestor).

    Files WITHOUT a package ancestor reach the single-file
    (`run_analysis<ast::Program>`) path; a file inside a package is discovered as
    a package graph and takes the `SourceGraph` path, which is exercised by the
    project corpus instead. Filtering here keeps the two halves disjoint and each
    non-vacuity assertion exact.
    """
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
    """Package-arrival (label, argv) pairs, one per fixture command.

    A fixture is any `ahfl.toml` under a project fixture root that declares a
    workflow target; it is driven as `--manifest <path> --target workflow
    --sysroot <repo>`, the exact shape `ahflc.check.*` uses. Fixtures without a
    workflow target are skipped rather than guessed at. The label is built here
    (not reconstructed from argv positions) so it stays correct if the argument
    shape changes.
    """
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

    # Both runs set the trace flag so the route line is observable on each side;
    # only the engine run sets the route flag. The line is stripped before the
    # stderr comparison below.
    direct_env = dict(os.environ)
    direct_env.pop("AHFL_QUERY_ENGINE", None)
    direct_env["AHFL_QUERY_ENGINE_TRACE"] = "1"

    engine_env = dict(os.environ)
    engine_env["AHFL_QUERY_ENGINE"] = "1"
    engine_env["AHFL_QUERY_ENGINE_TRACE"] = "1"

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
    traced = 0
    expected_traced = 0
    failures = 0

    # One (label, argv) pair per command to compare. The file corpus is bare
    # single files; the project corpus is package/workspace arrivals. Both halves
    # assert the same invariants, so they share the loop.
    file_runs = [
        (f"{' '.join(command)} {path.relative_to(repo_root)}", [*command, str(path)])
        for path in corpus
        for command in FILE_COMMANDS
    ]
    file_compared = len(file_runs)
    project_compared = len(project_runs)

    for label, args in file_runs + project_runs:
        direct = run(ahflc, args, direct_env)
        engine = run(ahflc, args, engine_env)

        if direct.returncode != engine.returncode:
            fail(f"{label}: exit {direct.returncode} (direct) != {engine.returncode} (engine)")
            failures += 1
            continue

        direct_route = route_of(direct.stderr)
        engine_route = route_of(engine.stderr)

        # Analysis-reached must agree: a parse error returns before the route
        # line on both sides, so one side reaching analysis and the other not
        # is a real divergence, not a fixture quirk.
        if (direct_route is None) != (engine_route is None):
            fail(
                f"{label}: analysis reached on one route only "
                f"(direct={direct_route!r}, engine={engine_route!r})"
            )
            failures += 1
            continue
        if direct_route is not None and direct_route != "direct":
            fail(f"{label}: direct run reported route {direct_route!r}")
            failures += 1
            continue

        if direct.stdout != engine.stdout:
            fail(f"{label}: stdout differs between routes")
            failures += 1
            continue

        # stderr must match once the route line (present on both sides) is
        # removed: a genuine diagnostic difference still fails here.
        if without_route(direct.stderr) != without_route(engine.stderr):
            fail(f"{label}: stderr differs between routes besides the route line")
            failures += 1
            continue

        if engine_route is not None:
            # Analysis was reached. The engine run must have actually used the
            # engine — a fallback to direct would make the gate vacuous.
            if engine_route != "engine":
                fail(f"{label}: reached analysis but fell back to the direct route")
                failures += 1
                continue
            expected_traced += 1
            traced += 1
        compared += 1

    if compared == 0:
        fail("no command/files were compared (vacuous gate)")
        return 1
    if expected_traced == 0:
        fail("no run reached analysis through the engine route (vacuous gate)")
        return 1
    if traced != expected_traced:
        fail(
            f"engine route engaged for {traced} runs but {expected_traced} reached analysis: "
            "the route regressed to the direct pipeline for some inputs"
        )
        return 1

    print(
        f"query-engine CLI equivalence: {compared} runs "
        f"({file_compared} file + {project_compared} project), "
        f"{traced} served by the engine route"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
