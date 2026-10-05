#include "state_machine.hpp"

namespace {

// Transition table: [state][event] -> next state, -1 = invalid.
//   Init:   Submit->Review, Reject->Done
//   Review: Approve->Done, Reject->Init
//   Done:   no outgoing transitions
constexpr int kTable[3][3] = {
    {1, -1, 2},
    {-1, 2, 0},
    {-1, -1, -1},
};

}  // namespace

int next_state(State s, Event e) {
    return kTable[static_cast<int>(s)][static_cast<int>(e)];
}

bool is_final(State s) {
    return s == State::Done;
}

bool can_fire(State s, Event e) {
    return next_state(s, e) >= 0;
}

int guarded_next(State s, Event e, bool allow_fast_track) {
    if (s == State::Init && e == Event::Submit && !allow_fast_track) {
        return -1;
    }
    return next_state(s, e);
}

int outgoing(State s) {
    int count = 0;
    for (int e = 0; e < 3; ++e) {
        if (kTable[static_cast<int>(s)][e] >= 0) {
            ++count;
        }
    }
    return count;
}

int initial_state() {
    return 0;
}

int state_name_len(State s) {
    return static_cast<int>(s) * 2;
}
