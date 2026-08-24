// Narrow test suite for the mutation target.
//
// Exit code 0 => all assertions hold (the "green" baseline). Any non-zero
// exit means the suite caught a defect; the fallback runner interprets that
// as "mutant killed". The suite intentionally does NOT test scaled(), so a
// mutation there survives and the reported score is an honest 3/4.
#include "target.hpp"

#include <cstdio>

int main() {
    int failures = 0;

    // Kills classify_rel: classify(0) is -1 with "<=", but +1 with "<".
    if (classify(0) != -1) { ++failures; }
    if (classify(5) != 1) { ++failures; }

    // Kills add_arith: add(2,3) is 5 with "+", but -1 with "-".
    if (add(2, 3) != 5) { ++failures; }

    // Kills is_valid_rel: is_valid(10) is true with ">=", but false with ">".
    if (is_valid(10) != true) { ++failures; }
    if (is_valid(9) != false) { ++failures; }

    // NOTE: scaled() is intentionally left untested.

    if (failures != 0) {
        std::printf("FAIL: %d assertion(s) failed\n", failures);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
