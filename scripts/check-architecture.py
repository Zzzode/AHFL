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
# The Payload variant in semantics/types.hpp has a fixed cardinality of 19 and
# MUST NOT include the legacy composite sugar types OptionalT / ListT / SetT /
# MapT. Composites are now encoded through nominal generics on StructT / EnumT.
# This Python gate runs as a CTest (ahfl.architecture.boundaries) alongside the
# compile-time static_assert gates in types.hpp so changes are rejected with a
# crisp message in CI before the entire project is rebuilt.
TYPES_HEADER = INCLUDE / "ahfl" / "compiler" / "semantics" / "types.hpp"
AST_HEADER = INCLUDE / "ahfl" / "compiler" / "frontend" / "ast.hpp"
IR_EXPR_HEADER = INCLUDE / "ahfl" / "compiler" / "ir" / "expr.hpp"
TYPECHECK_INTERNAL_HEADER = SRC / "compiler" / "semantics" / "typecheck_internal.hpp"
TYPECHECK_DECLS_SOURCE = SRC / "compiler" / "semantics" / "typecheck_decls.cpp"
FORMATTER_SOURCE = SRC / "tooling" / "formatter" / "formatter_api.cpp"
LEGACY_FORMATTER_SOURCE = SRC / "tooling" / "formatter" / "formatter.cpp"
LEGACY_RUNTIME_FACT_SOURCES = (
    SRC / "pipeline" / "execution" / "runtime_session",
    SRC / "pipeline" / "execution" / "execution_journal",
    SRC / "pipeline" / "execution" / "replay_view",
    SRC / "pipeline" / "observation",
    SRC / "pipeline" / "persistence",
)
PRODUCT_SURFACE_DOCUMENTS = (
    ROOT / "README.md",
    ROOT / "README.zh.md",
    ROOT / "docs" / "reference" / "cli-commands.zh.md",
    ROOT / "docs" / "reference" / "user-guide-cli.zh.md",
    ROOT / "docs" / "reference" / "user-guide-execution.zh.md",
    ROOT / "docs" / "reference" / "user-guide-assurance.zh.md",
    ROOT / "docs" / "reference" / "user-guide-overview.zh.md",
    ROOT / "docs" / "reference" / "contributor-guide.zh.md",
    ROOT / "docs" / "reference" / "native-runtime-artifacts.zh.md",
    ROOT / "docs" / "design" / "native-runtime-architecture.zh.md",
    ROOT / "docs" / "design" / "cli-pipeline-architecture.zh.md",
    ROOT / "docs" / "design" / "ir-backend-architecture.zh.md",
    ROOT / "docs" / "plans" / "project-status.zh.md",
)
RETIRED_PRODUCT_SURFACE_PATTERNS = (
    r"\bahflc\s+emit-runtime-session\b",
    r"\bahflc\s+emit-execution-journal\b",
    r"\bahflc\s+emit-replay-view\b",
    r"\bahflc\s+emit-audit-report\b",
    r"\bahflc\s+emit-scheduler-(?:snapshot|review)\b",
    r"\bahflc\s+emit-checkpoint-(?:record|review)\b",
    r"\bahflc\s+emit-persistence-(?:descriptor|review)\b",
    r"\bahflc\s+emit-store-import-(?:descriptor|review)\b",
    r"\bahflc\s+emit-durable-store-import-[a-z0-9-]+\b",
    r"\bahflc\s+emit-provider-artifact\b",
    r"\bahflc\s+emit\s+(?:runtime-session|execution-journal|replay-view|audit-report)\b",
    r"\bahflc\s+emit\s+scheduler-(?:snapshot|review)\b",
    r"\bahflc\s+emit\s+checkpoint-(?:record|review)\b",
    r"\bahflc\s+emit\s+persistence-(?:descriptor|review)\b",
    r"\bahflc\s+emit\s+store(?:/|-import-)[a-z0-9-]+\b",
    r"durable-store-import-(?:architecture|reference)\.zh\.md",
)
RETIRED_RUNTIME_FACT_PATTERNS = (
    r"ahfl\.reference-provider-audit\.v1",
    r"(?:pre-crash-)?provider-audit\.json",
)
EXPECTED_PAYLOAD_ARITY = 19
FORBIDDEN_PAYLOAD_SUGARS = ("OptionalT", "ListT", "SetT", "MapT")
ALLOWED_PAYLOAD_ALTERNATIVES = (
    "AnyT",
    "NeverT",
    "ErrorT",
    "UnitT",
    "BoolT",
    "IntT",
    "BoundedIntT",
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

AST_DECL_ALTERNATIVES = (
    "ModuleDecl",
    "ImportDecl",
    "UseDecl",
    "ConstDecl",
    "TypeAliasDecl",
    "StructDecl",
    "EnumDecl",
    "CapabilityDecl",
    "PredicateDecl",
    "AgentDecl",
    "ContractDecl",
    "FlowDecl",
    "WorkflowDecl",
    "FnDecl",
    "TraitDecl",
    "ImplDecl",
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


def check_ast_declaration_shape(failures: list[str]) -> None:
    """Require value-semantic, variant-backed top-level AST declarations."""
    if not AST_HEADER.is_file():
        failures.append(f"missing expected header: {AST_HEADER.relative_to(ROOT)}")
        return

    text = AST_HEADER.read_text()
    forbidden_patterns = (
        (r"\bstruct\s+Node\b", "top-level AST Node inheritance root"),
        (r"\bstruct\s+Decl\s*:\s*Node\b", "top-level AST declaration inheritance"),
        (r"\bclass\s+Visitor\b", "virtual top-level AST visitor"),
        (r"\bclass\s+RecursiveVisitor\b", "virtual recursive AST visitor"),
        (r"\bvirtual\s+", "virtual dispatch in the AST data model"),
        (r"\bOwned\s*<\s*Decl\s*>", "owned polymorphic declaration storage"),
    )
    for pattern, label in forbidden_patterns:
        match = re.search(pattern, text)
        if match is not None:
            line = text.count("\n", 0, match.start()) + 1
            failures.append(
                f"{AST_HEADER.relative_to(ROOT)}:{line}: forbidden {label}; "
                "top-level declarations must use a flat std::vector<Decl> store "
                "where Decl is a std::variant-backed value"
            )

    variant_match = re.search(
        r"using\s+Decl\s*=\s*std::variant\s*<(?P<body>.*?)>\s*;",
        text,
        flags=re.DOTALL,
    )
    if variant_match is None:
        failures.append(
            f"{AST_HEADER.relative_to(ROOT)}: missing canonical "
            "`using Decl = std::variant<...>;` declaration"
        )
    else:
        alternatives = re.findall(r"\b([A-Za-z_]\w*Decl)\b", variant_match.group("body"))
        if alternatives != list(AST_DECL_ALTERNATIVES):
            failures.append(
                f"{AST_HEADER.relative_to(ROOT)}: top-level Decl variant mismatch; "
                f"expected {list(AST_DECL_ALTERNATIVES)!r}, found {alternatives!r}"
            )

    if LEGACY_FORMATTER_SOURCE.exists():
        failures.append(
            f"{LEGACY_FORMATTER_SOURCE.relative_to(ROOT)}: legacy formatter source must not "
            "reappear; formatter_api.cpp owns the public lossless and standalone-fragment paths"
        )

    if not FORMATTER_SOURCE.is_file():
        failures.append(f"missing expected formatter source: {FORMATTER_SOURCE.relative_to(ROOT)}")
        return

    formatter_text = FORMATTER_SOURCE.read_text()
    for pattern, label in (
        (r"\bdynamic_cast\s*<", "dynamic_cast"),
        (r"\bAstFormatter\b", "legacy AstFormatter"),
    ):
        match = re.search(pattern, formatter_text)
        if match is not None:
            line = formatter_text.count("\n", 0, match.start()) + 1
            failures.append(
                f"{FORMATTER_SOURCE.relative_to(ROOT)}:{line}: forbidden {label}; "
                "the formatter must use the lossless source path and standalone "
                "variant visitors only"
            )


def check_ir_expression_effect_shape(failures: list[str]) -> None:
    """Require inferred ExprEffect to survive the Typed HIR -> IR boundary."""
    if not IR_EXPR_HEADER.is_file():
        failures.append(f"missing expected header: {IR_EXPR_HEADER.relative_to(ROOT)}")
        return

    text = IR_EXPR_HEADER.read_text()
    expr_match = re.search(r"struct\s+Expr\s*\{(?P<body>.*?)\n\};", text, flags=re.DOTALL)
    if expr_match is None:
        failures.append(f"{IR_EXPR_HEADER.relative_to(ROOT)}: missing canonical IR Expr record")
        return
    if re.search(r"\bExprEffect\s+effect\b", expr_match.group("body")) is None:
        failures.append(
            f"{IR_EXPR_HEADER.relative_to(ROOT)}: IR Expr must retain Typed HIR ExprEffect; "
            "assurance and formal consumers may not rescan AST to recover expression effects"
        )


def check_sema_ownership_shape(failures: list[str]) -> None:
    """Require the four Phase-2 Sema owners instead of a monolithic driver."""
    for path in (TYPECHECK_INTERNAL_HEADER, TYPECHECK_DECLS_SOURCE):
        if not path.is_file():
            failures.append(f"missing expected Sema source: {path.relative_to(ROOT)}")
            return

    header = TYPECHECK_INTERNAL_HEADER.read_text()
    declarations = TYPECHECK_DECLS_SOURCE.read_text()
    for owner in ("DeclarationSema", "FlowWorkflowSema", "ConstSema"):
        if re.search(rf"\bclass\s+{owner}\b", header) is None:
            failures.append(
                f"{TYPECHECK_INTERNAL_HEADER.relative_to(ROOT)}: missing Phase-2 owner `{owner}`"
            )

    for retired in ("EnvironmentBuilder", "ContractSema", "FlowSema", "WorkflowSema", "FnSema", "ImplSema"):
        match = re.search(rf"\bclass\s+{retired}\b", header)
        if match is not None:
            line = header.count("\n", 0, match.start()) + 1
            failures.append(
                f"{TYPECHECK_INTERNAL_HEADER.relative_to(ROOT)}:{line}: fragmented Sema owner "
                f"`{retired}` must be consolidated into DeclarationSema or FlowWorkflowSema"
            )

    driver_build = re.search(r"\bTypeCheckPass::build_[A-Za-z0-9_]+\s*\(", declarations)
    if driver_build is not None:
        line = declarations.count("\n", 0, driver_build.start()) + 1
        failures.append(
            f"{TYPECHECK_DECLS_SOURCE.relative_to(ROOT)}:{line}: declaration builders must be "
            "owned by DeclarationSema, not TypeCheckPass"
        )


def check_runtime_fact_source_shape(failures: list[str]) -> None:
    """Require the execution event store to be the only runtime fact source."""
    for path in LEGACY_RUNTIME_FACT_SOURCES:
        if path.exists():
            failures.append(
                f"{path.relative_to(ROOT)}: legacy runtime artifact fact source must be removed; "
                "replay, audit, scheduler, checkpoint, and recovery facts must project from "
                "WorkflowResult.events"
            )

    catalog = SRC / "tooling" / "cli" / "command_catalog.hpp"
    if catalog.is_file():
        text = catalog.read_text()
        for command in (
            "EmitRuntimeSession",
            "EmitExecutionJournal",
            "EmitReplayView",
            "EmitAuditReport",
            "EmitSchedulerSnapshot",
            "EmitSchedulerReview",
            "EmitCheckpointRecord",
            "EmitCheckpointReview",
            "EmitPersistenceDescriptor",
            "EmitPersistenceReview",
            "EmitExportManifest",
            "EmitExportReview",
            "EmitStoreImportDescriptor",
            "EmitStoreImportReview",
            "EmitDurableStoreImportRequest",
            "EmitDurableStoreImportReview",
            "EmitDurableStoreImportDecision",
            "EmitDurableStoreImportReceipt",
            "EmitDurableStoreImportReceiptPersistenceRequest",
            "EmitDurableStoreImportDecisionReview",
            "EmitDurableStoreImportReceiptReview",
            "EmitDurableStoreImportReceiptPersistenceReview",
            "EmitDurableStoreImportReceiptPersistenceResponse",
            "EmitDurableStoreImportReceiptPersistenceResponseReview",
            "EmitDurableStoreImportAdapterExecution",
            "EmitDurableStoreImportRecoveryPreview",
        ):
            match = re.search(rf"\b{command}\b", text)
            if match is not None:
                line = text.count("\n", 0, match.start()) + 1
                failures.append(
                    f"{catalog.relative_to(ROOT)}:{line}: legacy proxy command `{command}` "
                    "must be deleted instead of preserving a second runtime artifact chain"
                )

    provider_catalog = SRC / "tooling" / "cli" / "provider" / "provider_artifact_catalog.hpp"
    if provider_catalog.is_file():
        failures.append(
            f"{provider_catalog.relative_to(ROOT)}: proxy durable-store provider artifact "
            "catalog must be removed; real provider/runtime evidence belongs to execution events"
        )

    for root in (SRC, INCLUDE, ROOT / "tests"):
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in {".cpp", ".hpp", ".py"}:
                continue
            text = path.read_text(errors="replace")
            for pattern in RETIRED_RUNTIME_FACT_PATTERNS:
                match = re.search(pattern, text)
                if match is None:
                    continue
                line = text.count("\n", 0, match.start()) + 1
                failures.append(
                    f"{path.relative_to(ROOT)}:{line}: parallel provider audit fact source "
                    f"`{match.group(0)}` must not bypass ExecutionEventStore"
                )

    event_header = INCLUDE / "ahfl" / "runtime" / "execution_event.hpp"
    if event_header.is_file() and "CapabilityUsageRecorded" not in event_header.read_text():
        failures.append(
            f"{event_header.relative_to(ROOT)}: provider token/cost usage must be represented "
            "by the canonical execution event model"
        )


def check_documented_product_surface(failures: list[str]) -> None:
    """Prevent removed proxy commands from returning through maintained docs."""
    for path in PRODUCT_SURFACE_DOCUMENTS:
        if not path.is_file():
            failures.append(f"missing maintained product-surface document: {path.relative_to(ROOT)}")
            continue
        text = path.read_text()
        for pattern in RETIRED_PRODUCT_SURFACE_PATTERNS:
            match = re.search(pattern, text)
            if match is None:
                continue
            line = text.count("\n", 0, match.start()) + 1
            failures.append(
                f"{path.relative_to(ROOT)}:{line}: retired runtime proxy surface "
                f"`{match.group(0)}` must not be advertised; document canonical "
                "WorkflowRuntime events, projections, and recovery instead"
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
    # (c) Top-level AST declaration representation (Phase 2 roadmap).
    check_ast_declaration_shape(failures)
    # (d) Expression effects survive into the backend-facing IR.
    check_ir_expression_effect_shape(failures)
    # (e) Typecheck ownership follows the four Phase-2 Sema layers.
    check_sema_ownership_shape(failures)
    # (f) Runtime execution facts have one canonical event source (Phase 1 roadmap).
    check_runtime_fact_source_shape(failures)
    # (g) Maintained docs cannot re-advertise the retired proxy product surface.
    check_documented_product_surface(failures)

    if failures:
        print("architecture check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("architecture check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
