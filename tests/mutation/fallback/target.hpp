// Self-contained mutation-testing target.
//
// This translation unit is deliberately tiny and dependency-free so the
// fallback mutation runner (run_fallback_mutation.sh) can rebuild it in
// isolation, without linking against any AHFL library. It exercises the
// mutation classes referenced in mutation_config.json plus several additional
// operator categories a real mutation tool would hit (boolean connective,
// unary negation, constant replacement, comparison boundary, off-by-one loop
// bound) on a COPY of the source, so the real source tree is never modified.
#pragma once

// Returns -1 for non-positive input, +1 otherwise.
int classify(int x);

// Integer addition.
int add(int a, int b);

// Threshold predicate: true iff n reaches the inclusive lower bound.
bool is_valid(int n);

// Doubling. Intentionally left UNTESTED by target_test.cpp so that a mutation
// on this function survives, yielding an honest sub-100% mutation score.
int scaled(int v);

// Inclusive range check. Uses a boolean AND connective (mutation target:
// && -> ||).
bool in_range(int lo, int hi, int x);

// Logical negation. Uses a unary ! operator (mutation target: ! removal).
bool negate(bool a);

// Returns the additive identity. Uses a literal constant (mutation target:
// return 0 -> return 1).
int origin();

// Equality predicate. Uses == (mutation target: == -> !=).
bool equals(int a, int b);

// Counts iterations from 0 up to (but excluding) n; returns n for n >= 0.
// Uses a strict loop bound (mutation target: off-by-one < -> <=).
int count_to(int n);
