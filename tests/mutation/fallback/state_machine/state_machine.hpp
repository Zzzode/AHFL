// Self-contained mutation-testing target: agent-style state machine.
//
// This is target 3 of 3 for the fallback mutation runner (see
// arithmetic/arithmetic.hpp for target 1 and structured_writer/ for target 2).
// Like the others it is deliberately small and dependency-free so
// run_fallback_mutation.sh can rebuild it in isolation. It models the
// transition-table lookup at the heart of every AHFL agent: a flat
// [state][event] -> next-state table, terminal-state checks, guard-aware
// transitions, and outgoing-edge counting. The mutants here (table index
// swap, terminal equality, validity boundary, guard connective, guard
// negation, count boundary, constant replacement) cover a genuinely
// different bug family from both the arithmetic operator swaps and the
// JSON-writer punctuation changes.
//
// Its exact per-mutant outcomes are pinned in state_machine.target.json.
#pragma once

#include <cstdint>

enum class State : std::uint8_t { Init = 0, Review = 1, Done = 2 };
enum class Event : std::uint8_t { Submit = 0, Approve = 1, Reject = 2 };

// Transition table lookup: next_state[state][event], -1 = invalid.
int next_state(State s, Event e);

// True iff s is a terminal (final) state.
bool is_final(State s);

// True iff (s, e) is a valid transition in the table.
bool can_fire(State s, Event e);

// Guarded transition: Init+Submit is blocked when allow_fast_track is false.
// All other (s, e) pairs delegate to the plain table lookup.
int guarded_next(State s, Event e, bool allow_fast_track);

// Count of valid outgoing transitions from s.
int outgoing(State s);

// Returns the initial state index.
int initial_state();

// Intentionally left UNTESTED by state_machine_test.cpp so that a mutation
// on this function survives, yielding an honest sub-100% mutation score.
int state_name_len(State s);
