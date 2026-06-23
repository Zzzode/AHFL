#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
INCLUDE = ROOT / "include"

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

# --- ahfl::types::Payload variant shape gate (P5.4 / R-01) -------------------
#
# The Payload variant in semantics/types.hpp has a fixed cardinality of 18 and
# MUST NOT include the legacy composite sugar types OptionalT / ListT / SetT /
# MapT. Composites are now encoded through nominal generics on StructT / EnumT.
# This Python gate runs as a CTest (ahfl.architecture.boundaries) alongside the
# compile-time static_assert gates in types.hpp so changes are rejected with a
# crisp message in CI before the entire project is rebuilt.
TYPES_HEADER = INCLUDE / "ahfl" / "compiler" / "semantics" / "types.hpp"
EXPECTED_PAYLOAD_ARITY = 18
FORBIDDEN_PAYLOAD_SUGARS = ("OptionalT", "ListT", "SetT", "MapT")
ALLOWED_PAYLOAD_ALTERNATIVES = (
    "AnyT",
    "NeverT",
    "ErrorT",
    "UnitT",
    "BoolT",
    "IntT",
    "FloatT",
    "StringT",
    "BoundedStringT",
    "UUIDT",
    "TimestampT",
    "DurationT",
    "DecimalT",
    "StructT",
    "EnumT",
    "EnumVariantT",
    "FnT",
    "TypeVarT",
)


def domain_for(path: Path) -> str | None:
    rel = path.relative_to(SRC)
    return rel.parts[0] if rel.parts else None


def is_project_internal(include: str) -> bool:
    return include.startswith(ALLOWED_PREFIXES) or include.startswith(STALE_PREFIXES)


def check_payload_shape(failures: list[str]) -> None:
    """Validate ahfl::types::Payload arity + forbidden-sugar invariants."""
    if not TYPES_HEADER.is_file():
        failures.append(f"missing expected header: {TYPES_HEADER.relative_to(ROOT)}")
        return

    text = TYPES_HEADER.read_text()
    lines = text.splitlines()

    # 1. Locate the `using Payload = std::variant<...>;` block. The definition
    #    spans multiple lines: `using Payload = std::variant<` on one line,
    #    alternatives on following lines, closing `>;` on the last line.
    start = None
    end = None
    for idx, line in enumerate(lines):
        stripped = line.lstrip()
        if stripped.startswith("using Payload ") and "std::variant" in stripped:
            start = idx
            continue
        if start is not None and stripped.rstrip().endswith(">;"):
            end = idx
            break

    if start is None or end is None:
        failures.append(
            f"{TYPES_HEADER.relative_to(ROOT)}: could not locate the "
            "`using Payload = std::variant<...>` definition block. The "
            "architecture shape gate requires the canonical block form."
        )
        return

    block_lines = lines[start : end + 1]
    block_text = "\n".join(block_lines)

    # 2. Extract every C++ identifier inside the variant template argument
    #    list. We keep this intentionally narrow: only `FooT`-style identifiers
    #    are considered (payload alternatives are all named *T).
    identifiers = re.findall(r"\b([A-Za-z_]\w*T)\b", block_text)

    # 3. Strip the opening "variant" token (appears in `std::variant<`).
    identifiers = [tok for tok in identifiers if tok != "variant"]

    # 4. Forbidden sugars (fail fast, crisper message than cardinality check).
    sugar_hits = [name for name in identifiers if name in FORBIDDEN_PAYLOAD_SUGARS]
    if sugar_hits:
        # Provide file-relative line numbers for the first occurrence each.
        for sugar in sugar_hits:
            for local_no, line in enumerate(block_lines, start=1):
                if re.search(rf"\b{re.escape(sugar)}\b", line):
                    abs_line = start + local_no
                    failures.append(
                        f"{TYPES_HEADER.relative_to(ROOT)}:{abs_line}: forbidden "
                        f"sugar alternative `{sugar}` found inside "
                        f"ahfl::types::Payload. Composites must live on nominal "
                        f"generics (StructT/EnumT), not as variant members. "
                        f"See P5.4 / R-01."
                    )
                    break

    # 5. Cardinality gate. Must match exactly EXPECTED_PAYLOAD_ARITY.
    if len(identifiers) != EXPECTED_PAYLOAD_ARITY:
        failures.append(
            f"{TYPES_HEADER.relative_to(ROOT)}:{start + 1}: "
            f"ahfl::types::Payload arity drift: found {len(identifiers)} "
            f"alternatives, expected {EXPECTED_PAYLOAD_ARITY}. "
            f"Update both the C++ static_assert in types.hpp and this Python "
            f"gate together. Alternatives found: {identifiers!r}."
        )

    # 6. Exact-set gate (order-independent). Catches typos like `TypVarT` that
    #    would otherwise slip past both counts above.
    if set(identifiers) != set(ALLOWED_PAYLOAD_ALTERNATIVES):
        missing = sorted(set(ALLOWED_PAYLOAD_ALTERNATIVES) - set(identifiers))
        extra = sorted(set(identifiers) - set(ALLOWED_PAYLOAD_ALTERNATIVES))
        failures.append(
            f"{TYPES_HEADER.relative_to(ROOT)}:{start + 1}: "
            f"ahfl::types::Payload alternative set mismatch. "
            f"Missing expected alternatives: {missing!r}. "
            f"Unexpected alternatives: {extra!r}. "
            f"Update both the Payload definition and this gate atomically."
        )


def main() -> int:
    failures: list[str] = []

    # (a) Include-order / cross-layer boundary check (original gate).
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

    # (b) Payload variant shape gate (P5.4 / R-01).
    check_payload_shape(failures)

    if failures:
        print("architecture check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("architecture check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
