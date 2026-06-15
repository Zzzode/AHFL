#!/usr/bin/env python3

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


REQUIRED_TEXT: dict[str, tuple[str, ...]] = {
    "docs/README.md": (
        "ir-backend-architecture.zh.md",
        "ir-format.zh.md",
        "cli-commands.zh.md",
    ),
    "docs/design/ir-backend-architecture.zh.md": (
        "```mermaid",
        "emit opt-ir-json",
        "AHFL_OPT_IR_V1",
        "AHFL_TYPED_HIR_CACHE_V1",
        "PathRootKind",
        "AnalysisBundle::source_program_revision",
        "artifact-only",
        "readonly",
    ),
    "docs/reference/ir-format.zh.md": (
        "emit opt-ir-json",
        "AHFL_OPT_IR_V1",
        "skipped_temporal",
        "Program::analysis_revision",
        "artifact-only",
        "readonly",
    ),
    "docs/reference/cli-commands.zh.md": (
        "ahflc emit-opt-ir-json",
        "emit opt-ir-json",
        "AHFL_OPT_IR_V1",
        "artifact-only",
    ),
}

FORBIDDEN_IR_DESIGN_FRAGMENTS = (
    "\u2500",
    "\u2502",
    "\u250c",
    "\u2510",
    "\u2514",
    "\u2518",
    "+---",
    "|---",
)


def main() -> int:
    failures: list[str] = []

    for relative_path, required_fragments in REQUIRED_TEXT.items():
        path = ROOT / relative_path
        if not path.is_file():
            failures.append(f"{relative_path}: missing required IR documentation file")
            continue

        text = path.read_text()
        for fragment in required_fragments:
            if fragment not in text:
                failures.append(f"{relative_path}: missing required fragment {fragment!r}")

    design_path = ROOT / "docs/design/ir-backend-architecture.zh.md"
    if design_path.is_file():
        design_text = design_path.read_text()
        for fragment in FORBIDDEN_IR_DESIGN_FRAGMENTS:
            if fragment in design_text:
                failures.append(
                    "docs/design/ir-backend-architecture.zh.md: "
                    f"architecture diagrams must use Mermaid, found {fragment!r}"
                )

    if failures:
        print("IR documentation sync check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("IR documentation sync check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
