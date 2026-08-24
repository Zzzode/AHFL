// Narrow test suite for the mutation target.
//
// Exit code 0 => all assertions hold (the "green" baseline). Any non-zero
// exit means the suite caught a defect; the fallback runner interprets that
// as "mutant killed". The suite intentionally does NOT test scaled(), so a
// mutation there survives and the reported score is honest (< 100%).
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

    // Kills bool_connective: in_range(0,10,20) is false with "&&", but the
    // "||" mutant makes it true (20 >= 0 is enough).
    if (in_range(0, 10, 5) != true) { ++failures; }
    if (in_range(0, 10, 20) != false) { ++failures; }

    // Kills unary_negation: negate(true) is false with "!", true without it.
    if (negate(true) != false) { ++failures; }
    if (negate(false) != true) { ++failures; }

    // Kills const_replace: origin() is 0, but the mutant returns 1.
    if (origin() != 0) { ++failures; }

    // Kills eq_boundary: equals(3,3) is true with "==", false with "!=".
    if (equals(3, 3) != true) { ++failures; }
    if (equals(3, 4) != false) { ++failures; }

    // Kills loop_bound: count_to(3) is 3 with "<", but 4 with the "<=" mutant.
    if (count_to(3) != 3) { ++failures; }
    if (count_to(0) != 0) { ++failures; }

    // NOTE: scaled() is intentionally left untested (expected survivor).

    if (failures != 0) {
        std::printf("FAIL: %d assertion(s) failed\n", failures);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
