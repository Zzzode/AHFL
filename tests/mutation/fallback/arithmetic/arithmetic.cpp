// Self-contained mutation-testing target implementation: integer arithmetic.
//
// The exact expression text below is what the fallback runner mutates via
// fixed, deterministic string substitutions. The mutant set itself is NOT
// here: it lives next to this file in target_mutants.json, so run_fallback_mutation.sh
// stays target-agnostic. Do not reformat these expressions casually: the
// keyed patterns there match them literally.
#include "arithmetic.hpp"

int classify(int x) {
    if (x <= 0) {   // MUT classify_rel (see target_mutants.json)
        return -1;
    }
    return 1;
}

int add(int a, int b) {
    return a + b;   // MUT add_arith (see target_mutants.json)
}

bool is_valid(int n) {
    return n >= 10; // MUT is_valid_rel (see target_mutants.json)
}

int scaled(int v) {
    return v * 2;   // MUT scaled_arith (survives: untested)
}

bool in_range(int lo, int hi, int x) {
    return x >= lo && x <= hi;   // MUT bool_connective (see target_mutants.json)
}

bool negate(bool a) {
    return !a;      // MUT unary_negation (see target_mutants.json)
}

int origin() {
    return 0;       // MUT const_replace (see target_mutants.json)
}

bool equals(int a, int b) {
    return a == b;  // MUT eq_boundary (see target_mutants.json)
}

int count_to(int n) {
    int c = 0;
    for (int i = 0; i < n; ++i) {   // MUT loop_bound (see target_mutants.json)
        ++c;
    }
    return c;
}
