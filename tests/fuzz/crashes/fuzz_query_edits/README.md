# fuzz_query_edits

Target for the RFC 0027 (KR6.11-S5) differential edit-sequence property:
`fuzz_query_edits` decodes its byte input into a script of text edits
(insert / delete / replace / equal re-set) and drives them through a long-lived
`ahfl::query::FrontendQueries` engine, asserting after every edit that the
incremental result equals a cold-cache recomputation and that only text-changing
edits recompute. It shares its property definition with the seeded doctest guard
at `tests/unit/compiler/query/incremental_equiv_property.cpp` via
`tests/common/query_edit_property.hpp`.

A finding here is an invalidation-invariant violation (stale memo served, or a
recompute with no cause), not a memory error: the harness calls `std::abort()`
with the mismatching step and the two projections, so libFuzzer saves the input
as a crash artifact. Drop the saved input into `tests/fuzz/crashes/fuzz_query_edits/`
so `crash_replay.sh` keeps it as a permanent regression guard.

Build modes follow the repository-wide dual-mode pattern (see
`tests/fuzz/README.md` and `fuzz_parser.cpp`):

* default (fuzzing disabled) — compiles as `fuzz_query_edits_check`, a
  standalone executable that runs a fixed set of scripts (identity, empty,
  mutating, oversized) and is registered as `ahfl.fuzz.query_edits_check`.
* `-DAHFL_ENABLE_FUZZING=ON` — compiles the libFuzzer entry point
  `LLVMFuzzerTestOneInput` and registers the crash-replay lane.
