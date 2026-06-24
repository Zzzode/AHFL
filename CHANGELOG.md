# AHFL Change Log

All notable changes to the AHFL project will be documented in this file.
The format is inspired by [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Wave 15] - integration-wave-15 (2026-06-24)

**Baseline**: `82871444 test(assurance): obligations count invariant + JSON schema + status coverage` (integration-wave-14).

**Integration status**: No new sub-task commits were landed in this wave (implementation queue was empty at integration time). This wave carries the integration baseline forward with the same build and test posture as Wave 14.

### Changes merged
- (none)

### Build & test posture
- CMake configure: OK
- Full build: OK (100% targets linked)
- ctest total: 977 tests
- ctest passed: 974 (99%)
- Pre-existing failures (carried from integration-wave-14, tracked in tasks #335/#336/#340):
  - `ahfl.semantics.typed_hir_all` — typed HIR round-trip snapshots need source changes from in-progress Typed HIR migration (task #335: Batch 1 + task #336: Batch 2)
  - `ahfl.semantics.effects_all` — effect judgement capability serialization round-trip, same dependency
  - `ahflc.emit_ir_json.expr_temporal` — IR JSON golden expectation drift for temporal expression artifacts
- P5.0 `p5_smv_golden_lock` + negative_diag: PASS (no diff)

### Follow-up tracked
- #331 verification.obligations in assurance bundle (pending)
- #332 assurance obligations tests / schema / golden (pending)
- #335 Batch 1: A 类 typed_hir source + E1 effects sugar (in progress)
- #336 Batch 2: C 类 EffectJudgement serialization fix (in progress)
- #340 Batch 4: remaining 6 needle/diagnostic fixes + diagnostics capture (in progress)

---

## [Wave 14] - integration-wave-14

**Baseline**: integration-wave-12 (commit `c4de2500`).

### Changes merged
- P4.S7b: decreases bounded-rank SMV + `DECREASES_SHADOWED_RECEIVER` + golden test
- Assurance: obligations count invariant + JSON schema + status coverage

### Build & test posture
- Build: OK
- ctest: green on core suite; `p5_smv_golden_lock`: PASS
