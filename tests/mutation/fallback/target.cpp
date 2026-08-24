// Self-contained mutation-testing target implementation.
//
// The exact expression text below is what the fallback runner mutates via
// fixed, deterministic string substitutions (see run_fallback_mutation.sh).
// Do not reformat these expressions casually: the runner's mutant patterns
// match them literally.
#include "target.hpp"

int classify(int x) {
    if (x <= 0) {   // MUT classify_rel: "x <= 0" -> "x < 0"
        return -1;
    }
    return 1;
}

int add(int a, int b) {
    return a + b;   // MUT add_arith: "a + b" -> "a - b"
}

bool is_valid(int n) {
    return n >= 10; // MUT is_valid_rel: "n >= 10" -> "n > 10"
}

int scaled(int v) {
    return v * 2;   // MUT scaled_arith: "v * 2" -> "v + 2" (survives: untested)
}
