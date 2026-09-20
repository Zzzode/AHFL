#!/usr/bin/env python3
"""RFC 0027 P3 (KR6.11-S4): the CLI query-engine route equivalence gate.

`ahflc` can evaluate the frontend through the self-built query engine instead of
the direct pipeline, under `AHFL_QUERY_ENGINE=1` (see `query_engine_route_requested`
in `src/tooling/cli/cli_driver.cpp`). This gate is the "exhaustive equivalence
ctest" the S4 slice requires: for a corpus of single-file sources it runs the same
command twice — once on each route — and asserts the exit code, stdout, and
stderr are byte-identical. stdout is the artifact a user sees (IR JSON, SMV,
summaries), so byte-equality here is the CLI-level statement of RFC 0027's
migration criterion ("the query result is the same result").

Non-vacuity. An equivalence gate that silently fell back to the direct pipeline on
every input would pass trivially. The engine route emits a
`query-engine-route:` trace line on stderr *only* when it actually served the
analysis; with `AHFL_QUERY_ENGINE_TRACE=1` this gate asserts that every corpus
file produced the trace, so a regression that disables the route fails the gate
rather than hiding behind byte-equality.

Scope. Only the single-file (`ast::Program`) input shape can be routed today: a
`SourceGraph` is not `equality_comparable` and a SourceUnit's AST is not a
function of its text alone, so the package/workspace path stays on the direct
pipeline until `parse_project` is itself query-ified. This gate therefore covers
the bare-file corpus; the driver's own comment records the boundary.

Usage:
  query_engine_cli_equiv.py <ahflc> <repo-root> <scratch-dir>
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

# Bare-file commands whose output is deterministic and whose pipeline reaches
# resolve/typecheck (so the engine route has something to serve). `check` is the
# one command a bare single file can drive end to end: `emit ir` / `emit ir-json`
# refuse a bare file with a usage error (they require a package manifest), so they
# never reach `run_analysis` and are not part of this route-equivalence gate.
COMMANDS = (("check",),)

# Corpus roots scanned recursively for single-file `.ahfl` sources. Golden
# fixtures include malformed and semantically-erroring sources on purpose: route
# equivalence must hold for the failure projections too, not just success.
CORPUS_ROOTS = ("tests/golden", "examples")

# Upper bound so the gate stays a smoke-sized ctest. The corpus is walked in
# sorted order, which is deterministic run to run.
MAX_FILES = 200

TRACE_TOKEN = "query-engine-route:"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)


def has_package_ancestor(path: Path, repo_root: Path) -> bool:
    """Whether the file is inside an AHFL package (has an ahfl.toml ancestor).

    Only files WITHOUT a package ancestor reach the single-file
    (`run_analysis<ast::Program>`) path the engine can carry; a file inside a
    package is discovered as a package graph and takes the `SourceGraph` path,
    which is deliberately not routed yet. Filtering here keeps the gate's
    non-vacuity assertion exact instead of tolerating "some runs did not engage".
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

    import os

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

    compared = 0
    traced = 0
    expected_traced = 0
    failures = 0

    for path in corpus:
        relative = path.relative_to(repo_root)
        for command in COMMANDS:
            args = [*command, str(path)]
            direct = run(ahflc, args, direct_env)
            engine = run(ahflc, args, engine_env)

            label = f"{' '.join(command)} {relative}"
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
            def without_route(text: str) -> str:
                return "\n".join(
                    line for line in text.splitlines() if not line.startswith(TRACE_TOKEN)
                )

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
        f"query-engine CLI equivalence: {compared} runs over {len(corpus)} files, "
        f"{traced} served by the engine route"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
