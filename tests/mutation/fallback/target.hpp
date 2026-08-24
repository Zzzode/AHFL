// Self-contained mutation-testing target.
//
// This translation unit is deliberately tiny and dependency-free so the
// fallback mutation runner (run_fallback_mutation.sh) can rebuild it in
// isolation, without linking against any AHFL library. It exercises the
// mutation classes referenced in mutation_config.json (relational operator
// swaps, arithmetic operator swaps, scalar replacement) on a COPY of the
// source, so the real source tree is never modified.
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
