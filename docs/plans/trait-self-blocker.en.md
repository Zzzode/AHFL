---
name: ahfl-trait-self-blocker
description: AHFL trait system lacks Self keyword — generic trait impls (impl Eq for Option<T>) are blocked
metadata:
  type: project
---

# AHFL Trait System: Missing `Self` Keyword Blocks Generic Impls

## Status
B-5~B-9 trait impl batch (Option×7, Result×7, List/Set/Map×container-family, JsonValue×6, primitives×4) are **blocked** by a missing language feature.

## Root Cause
`signatures_match()` in `typecheck.cpp:1279` compares trait method param types by **pointer equality**. The trait declarations in `std/traits.ahfl` use concrete placeholder types (`Bool`, `Int`, `collections::List<Int>`) as receiver types, not `Self`. So `impl Eq for Option<T>` with `fn eq(self: Option<T>, other: Option<T>)` fails with `TRAIT_METHOD_SIGNATURE_MISMATCH: trait expects (Bool, Bool), impl provides (Option<T>, Option<T>)`.

## What's needed
1. `Self` keyword in trait method signatures (PHASE B per RFC)
2. Or: trait-level type params that get substituted at impl resolution time
3. signatures_match must perform Self substitution, not pointer equality

## Workaround used
None. The trait impl batch cannot proceed until Self support lands. The inherent impl path (methods on `impl<T> Option<T>`) works fine and covers most stdlib API needs.

## Cross-module variant refs (B-3)
No crash — works correctly with 3-segment syntax `Module::Enum::Variant(args)`. 2-segment `Module::Variant` correctly reports "unknown callable" (not a crash). Added 2 regression tests in `trait_impl.cpp`.

## DispatchTarget (C-5)
Fully implemented: struct in typed_hir.hpp, typecheck wiring in typecheck_expr.cpp + typecheck.cpp, JSON ser/de in typed_hir_serialization.cpp, and typed_hir_lower.cpp `render_method_target` uses stored dispatch first (falls back to re-scan for backward compat).
