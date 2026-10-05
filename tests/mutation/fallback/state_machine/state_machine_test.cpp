// Narrow test suite for the state_machine mutation target.
//
// Exit code 0 => all assertions hold (the "green" baseline). Any non-zero
// exit means the suite caught a defect; the fallback runner interprets that
// as "mutant killed". The suite intentionally does NOT test state_name_len(),
// so a mutation there survives and the reported score is honest (< 100%).
//
// The runner pins this suite's exact per-mutant outcome list in
// state_machine.target.json, so every deliberate behaviour here (including
// the intentional survivor) is a reviewed, machine-checked baseline entry.
#include "state_machine.hpp"

#include <cstdio>

int main() {
    int failures = 0;

    // Kills table_index_swap: next_state(Init, Reject) is 2 with [s][e],
    // but kTable[2][0] = -1 with swapped [e][s] indices.
    if (next_state(State::Init, Event::Reject) != 2) { ++failures; }
    if (next_state(State::Review, Event::Reject) != 0) { ++failures; }
    if (next_state(State::Done, Event::Submit) != -1) { ++failures; }

    // Kills final_eq_ne: is_final(Done) is true with ==, false with !=.
    if (is_final(State::Done) != true) { ++failures; }
    if (is_final(State::Init) != false) { ++failures; }

    // Kills can_fire_ge_gt: can_fire(Review, Reject) is true with >= (0 >= 0),
    // but false with > (0 > 0).
    if (can_fire(State::Review, Event::Reject) != true) { ++failures; }
    if (can_fire(State::Done, Event::Submit) != false) { ++failures; }

    // Kills guard_connective: guarded_next(Init, Submit, true) is 1 with &&
    // (guard does not block), but -1 with || (guard blocks).
    // Kills unary_negation: same case -- !true becomes true, guard blocks.
    if (guarded_next(State::Init, Event::Submit, true) != 1) { ++failures; }
    // guarded_next(Init, Submit, false) is -1 (guard blocks).
    if (guarded_next(State::Init, Event::Submit, false) != -1) { ++failures; }

    // Kills outgoing_ge_gt: outgoing(Review) is 2 with >= (Approve=2,
    // Reject=0), but 1 with > (only Approve=2).
    if (outgoing(State::Review) != 2) { ++failures; }
    if (outgoing(State::Done) != 0) { ++failures; }

    // Kills const_replace: initial_state() is 0, but the mutant returns 1.
    if (initial_state() != 0) { ++failures; }

    // NOTE: state_name_len() is intentionally left untested (expected survivor).

    if (failures != 0) {
        std::printf("FAIL: %d assertion(s) failed\n", failures);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
