// RFC 0027 P2 (KR6.11-S5): libFuzzer driver for the differential edit-sequence
// property.
//
// This is the fuzzing half of the same property tests/unit/compiler/query/
// incremental_equiv_property.cpp checks with seeded scripts: the input bytes are
// decoded into an edit sequence (see tests/common/query_edit_property.hpp:
// decode_edits) and driven through a long-lived FrontendQueries engine, asserting
// after every edit that the incremental result equals a cold-cache recomputation
// and that only text-changing edits recompute. The shared header is the single
// definition of the property; this file only supplies the byte-driven script and
// turns a violation into a __builtin_trap / abort so libFuzzer reports the input
// as a crash.
//
// Follows the AHFL_FUZZ_STANDALONE dual-mode pattern of fuzz_parser.cpp: with
// FUZZING off it compiles as a standalone self-check executable registered as
// ahfl.fuzz.query_edits_check, and its file argument / crash corpus slot
// (tests/fuzz/crashes/fuzz_query_edits) is wired by tests/fuzz/CMakeLists.txt.
//
// A *violation* here is not a memory error: it is the invalidation invariant
// breaking, which is exactly what the RFC wants the fuzzer pointed at.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "common/query_edit_property.hpp"

namespace {

using ahfl::test_support::query_edit::decode_edits;
using ahfl::test_support::query_edit::Edit;
using ahfl::test_support::query_edit::run_edit_sequence;
using ahfl::test_support::query_edit::SequenceOutcome;

// Cap the decoded script: every step costs three parses plus a resolve /
// typecheck / lower, so an unbounded script turns a fuzz iteration into a
// multi-second stall and starves the search loop.
constexpr std::size_t kMaxDecodedEdits = 12;

// A clean fixture so a short script that leaves the text intact still exercises
// the downstream (IR) side of the property. The fuzzer's own bytes determine the
// edits applied to it.
constexpr std::string_view kSeed = R"AHFL(
struct Request { value: Int; }
agent Worker {
    input: Request;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    transition Init -> Done;
}
flow for Worker {
    state Init { goto Done; }
    state Done { return Request { value: 0 }; }
}
)AHFL";

void check_edit_input(const uint8_t *data, std::size_t size) {
    if (size > 4096) {
        return; // keep the per-iteration parse budget bounded
    }
    const std::vector<Edit> edits = decode_edits(data, size, kMaxDecodedEdits);
    const SequenceOutcome outcome = run_edit_sequence("fuzz_query_edits.ahfl", kSeed, edits);
    if (outcome.mismatches != 0) {
        // A property violation is treated as a crash: the input is saved by
        // libFuzzer and becomes a permanent regression guard via crash_replay.sh.
        std::fprintf(stderr, "query edit property violated: %s\n", outcome.first_mismatch.c_str());
        std::abort();
    }
}

} // namespace

#ifdef AHFL_FUZZ_STANDALONE
int main() {
    std::printf("fuzz_query_edits standalone check\n");

    // Identity-only script: after the priming evaluation, no edit may recompute
    // and the projection must hold.
    const uint8_t identity[] = {3, 0, 0, 0, 0, 3, 0, 3, 0, 1};
    check_edit_input(identity, sizeof(identity));

    // An empty and a null input are legitimate (no edits).
    const uint8_t empty[] = {0};
    check_edit_input(empty, 0);
    check_edit_input(nullptr, 0);

    // Real mutating scripts, including a deliberately oversized one.
    const uint8_t mutate[] = {0, 1, 0, 0, 0, 1, 0, 2, 3, 7, 2, 0, 50, 9, 15, 0, 7, 0, 4, 2};
    check_edit_input(mutate, sizeof(mutate));

    std::vector<uint8_t> big(8192, 0x11);
    check_edit_input(big.data(), big.size());

    std::printf("  PASS: standalone query edit fuzz check\n");
    return 0;
}
#else
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
    check_edit_input(data, size);
    return 0;
}
#endif
