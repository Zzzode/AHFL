#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+["<]([^">]+)[">]')

ALLOWED_PREFIXES = (
    "ahfl/",
    "base/",
    "compiler/",
    "pipeline/",
    "runtime/",
    "tooling/",
    "verification/",
)
STALE_PREFIXES = (
    "support/",
    "json/",
    "validation/",
    "parser/",
    "frontend/",
    "semantics/",
    "ir/",
    "assurance/",
    "handoff/",
    "backends/",
    "passes/",
    "dry_run/",
    "runtime_session/",
    "execution_journal/",
    "replay_view/",
    "audit_report/",
    "scheduler_snapshot/",
    "checkpoint_record/",
    "persistence_descriptor/",
    "persistence_export/",
    "store_import/",
    "durable_store_import/",
    "evaluator/",
    "llm_provider/",
    "secret/",
    "formal/",
    "abi/",
    "dap/",
    "formatter/",
    "lsp/",
    "profiling/",
    "repl/",
    "telemetry/",
    "incremental/",
    "testing/",
    "package/",
    "cli/",
)
FORBIDDEN_DEPENDENCIES = {
    "base": ("compiler/", "pipeline/", "runtime/", "tooling/", "verification/"),
    "compiler": ("tooling/",),
    "pipeline": ("tooling/",),
    "runtime": ("tooling/",),
    "verification": ("tooling/",),
}


def domain_for(path: Path) -> str | None:
    rel = path.relative_to(SRC)
    return rel.parts[0] if rel.parts else None


def is_project_internal(include: str) -> bool:
    return include.startswith(ALLOWED_PREFIXES) or include.startswith(STALE_PREFIXES)


def main() -> int:
    failures: list[str] = []
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in {".cpp", ".hpp", ".h"}:
            continue
        domain = domain_for(path)
        for line_number, line in enumerate(path.read_text().splitlines(), start=1):
            match = INCLUDE_RE.match(line)
            if not match:
                continue

            include = match.group(1)
            if not is_project_internal(include):
                continue

            if include.startswith(STALE_PREFIXES):
                failures.append(
                    f"{path.relative_to(ROOT)}:{line_number}: stale internal include '{include}'"
                )
                continue

            for forbidden in FORBIDDEN_DEPENDENCIES.get(domain or "", ()):  # defensive default
                if include.startswith(forbidden):
                    failures.append(
                        f"{path.relative_to(ROOT)}:{line_number}: {domain}/ must not include '{include}'"
                    )

    if failures:
        print("architecture check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("architecture check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
