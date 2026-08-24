# Crash repro: `<sha256>`

Copy this file to `<sha256>.repro.md` next to its crash input and fill in.

| Field | Value |
|---|---|
| **Target** | fuzz_parser / fuzz_typecheck / fuzz_smv_emitter |
| **Input file** | `<sha256>` (this dir) |
| **sha256** | `<sha256>` |
| **Kind** | crash / leak / timeout / oom |
| **Discovered (UTC)** | YYYY-MM-DD |
| **Discovered by** | @github-handle / cron-N |
| **libFuzzer / Sanitizer** | e.g. clang 19.1.5, ubuntu:24.04 |
| **AHFL commit at discovery** | `<sha>` |
| **Fixed by** | PR #… / `<sha>` (blank while open) |
| **Status** | Open / Fixed |

## Symptom

One-line description of the failure (e.g. ASan stack top, exception type).

## Minimization

- Raw artifact size: N bytes → minimized: M bytes.
- Command used: `fuzz_<target> -minimize_crash=1 -runs=100000 -exact_artifact_path=... <raw>`

## Root cause

What actually went wrong, and where.

## Fix

Link the fixing PR / commit. After the fix lands, the
`ahfl.fuzz.<target>.crash_replay` CTest keeps this input as a permanent
regression guard.
