// RFC 0027 P2 (KR6.11-S5): the differential property guard.
//
// RFC 0027's Test Plan names this as THE core acceptance property of
// query-ification: "under batches of edits, the incremental engine's results must
// never diverge from a cold-cache full recomputation". The S3 guard proved the
// methodology on a single static comparison (query == direct for a corpus); this
// guard proves the *dynamic* half — that the invalidation machinery keeps that
// equality after an arbitrary sequence of edits, which is the invariant KR6.12
// depends on when it starts deleting the hand-maintained incremental subsystem.
//
// The property, the edit operators, and the observable projections all live in
// tests/common/query_edit_property.hpp (one definition, two drivers: this seeded
// doctest and tests/fuzz/fuzz_query_edits.cpp). Read that header first.
//
// Two properties are guarded here:
//
//   A. Seeded edit sequences. For each seed file, N deterministic edit sequences
//      mutate the text one edit at a time. After every edit the long-lived
//      engine's parse projection must equal (a) a fresh cold-cache engine on the
//      same final text and (b) an independently re-derived direct-frontend
//      projection; the engine-held AST must drive resolve/typecheck/lower to the
//      same bytes as a freshly parsed AST; and the parse recompute counter must
//      advance by exactly one iff the text changed (both over- and
//      under-invalidation are failures).
//
//   B. Cross-file invalidation precision. In a two-file engine, an edit sequence
//      against file A must not recompute file B at all — the "invalidates too
//      much" failure mode, asserted as an exact counter rather than a timing.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "common/query_edit_property.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ahfl::test_support::query_edit::Edit;
using ahfl::test_support::query_edit::run_edit_sequence;
using ahfl::test_support::query_edit::seeded_edits;
using ahfl::test_support::query_edit::SequenceOutcome;

// 500 sequences (RFC 0027 Test Plan: "random edit sequences"), spread over the
// corpus files selected below. Sequence length is seed-dependent so the guard
// covers both short (few cache states) and longer (many superseded memos)
// histories; a single fixed length would exercise exactly one of those.
constexpr std::size_t kSequences = 500;
constexpr std::size_t kMinSteps = 3;
constexpr std::size_t kMaxSteps = 9;

// Corpus selection bounds. Small seeds keep the per-step cost (three parses plus
// a downstream pipeline) inside a practical unit-test budget; the count floor is
// the RFC's "10+ corpus files". The floor is a *selection* invariant (how many
// files this guard needs), not a corpus-size threshold that unrelated golden
// cleanups would turn red. Selection spreads across subtrees (see
// collect_seeds) rather than taking a lexicographic prefix, so churn confined to
// one subtree cannot move the count across the floor: round-robin refills from
// the other subtrees and the count only falls once the *whole* corpus does.
constexpr std::size_t kMinSeedFiles = 10;
constexpr std::size_t kMaxSeedFiles = 14;
constexpr std::size_t kMaxSeedBytes = 1536;

// A small, complete, deliberately clean program: guarantees the downstream IR
// side of the property is non-vacuous (the golden corpus is dominated by
// intentionally malformed typecheck fixtures).
constexpr std::string_view kCleanSeed = R"AHFL(
struct Request { value: Int; }
struct Ctx { count: Int = 0; }

agent Worker {
    input: Request;
    context: Ctx;
    output: Ctx;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}
flow for Worker {
    state Init {
        let doubled = input.value + input.value;
        goto Done;
    }
    state Done { return Ctx { count: 0 }; }
}
)AHFL";

// A malformed seed: the erroring projection must be covered from step one, not
// only when an edit happens to break a clean file.
constexpr std::string_view kMalformedSeed = "agent {";

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::filesystem::path repo_root() {
#ifdef AHFL_SOURCE_DIR
    return std::filesystem::path{AHFL_SOURCE_DIR};
#else
    return std::filesystem::path{"."};
#endif
}

// Deterministic small-file subset of the same corpus roots the S3 equivalence
// guard walks (tests/golden/**, examples/**). The take is deterministic (sorted
// walk, size-capped) but *spread across subtrees* rather than a prefix of the
// lexicographic order: candidates are grouped by their directory relative to the
// repo root, each group is sorted by (size, path), and groups are drained
// round-robin — smallest first — until kMaxSeedFiles is reached. A prefix take
// would make the count a function of whichever two early-sorted subtrees sort
// first, so churn confined to them could drop the selection below the floor even
// though the rest of the corpus is untouched; round-robin refills from the other
// groups first and only hits the floor once the whole corpus drains. Selection is
// still identical on every run and every machine (no RNG, no filesystem order).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> collect_seeds() {
    std::vector<std::filesystem::path> candidates;
    const auto root = repo_root();
    for (const auto &relative : {"tests/golden", "examples"}) {
        const auto base = root / relative;
        std::error_code ec;
        if (!std::filesystem::is_directory(base, ec)) {
            continue;
        }
        std::filesystem::recursive_directory_iterator it(base, ec);
        for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file() || it->path().extension() != ".ahfl") {
                continue;
            }
            candidates.push_back(it->path());
        }
    }
    std::ranges::sort(candidates);

    // One bucket per containing directory, keyed by the root-relative directory
    // path so the grouping is stable across absolute checkout locations.
    struct Eligible {
        std::filesystem::path path;
        std::string text;
    };
    struct Group {
        std::string directory;
        std::vector<Eligible> entries;
    };
    std::vector<Group> groups;
    for (const auto &path : candidates) {
        const std::string text = read_file(path);
        if (text.empty() || text.size() > kMaxSeedBytes) {
            continue;
        }
        std::error_code ec;
        const std::string directory =
            std::filesystem::relative(path.parent_path(), root, ec).generic_string();
        auto group = std::ranges::find(groups, directory, &Group::directory);
        if (group == groups.end()) {
            groups.push_back(Group{directory, {}});
            group = std::prev(groups.end());
        }
        group->entries.push_back(Eligible{path, text});
    }
    std::ranges::sort(groups, {}, &Group::directory);
    for (auto &group : groups) {
        std::ranges::sort(group.entries, {}, [](const Eligible &entry) {
            return std::pair{entry.text.size(), entry.path.generic_string()};
        });
    }

    std::vector<std::pair<std::string, std::string>> seeds;
    for (std::size_t round = 0; seeds.size() < kMaxSeedFiles; ++round) {
        bool drained = true;
        for (auto &group : groups) {
            if (round >= group.entries.size()) {
                continue;
            }
            drained = false;
            if (seeds.size() >= kMaxSeedFiles) {
                break;
            }
            Eligible &entry = group.entries[round];
            seeds.emplace_back(entry.path.generic_string(), std::move(entry.text));
        }
        if (drained) {
            break;
        }
    }
    return seeds;
}

[[nodiscard]] std::size_t steps_for_seed(std::size_t sequence, std::size_t seed_index) {
    const std::size_t span = kMaxSteps - kMinSteps + 1;
    return kMinSteps + ((sequence * 7 + seed_index * 13 + 5) % span);
}

} // namespace

TEST_CASE("incremental engine matches cold-cache recomputation under seeded edit sequences") {
    const auto seeds = collect_seeds();
    INFO("corpus seeds selected: " << seeds.size());
    REQUIRE(seeds.size() >= kMinSeedFiles);

    // Two synthetic seeds in front of the corpus: one clean (drives the
    // resolve/typecheck/lower projection) and one malformed (drives the
    // diagnostic projection from the first edit).
    std::vector<std::pair<std::string, std::string>> pools;
    pools.emplace_back("hand_written.ahfl", std::string{kCleanSeed});
    pools.emplace_back("malformed.ahfl", std::string{kMalformedSeed});
    pools.insert(pools.end(), seeds.begin(), seeds.end());

    SequenceOutcome total;
    std::size_t sequences = 0;
    for (std::size_t sequence = 0; sequence < kSequences; ++sequence) {
        const std::size_t pool_index = sequence % pools.size();
        const auto &[display_name, seed_text] = pools[pool_index];
        const std::size_t steps = steps_for_seed(sequence, pool_index);
        // The seed mixes the sequence number with the file index so two
        // sequences over the same file do not replay the same script.
        const std::uint64_t edit_seed =
            0x5eed0000ULL + (static_cast<std::uint64_t>(sequence) << 16) +
            static_cast<std::uint64_t>(pool_index);
        const std::vector<Edit> edits = seeded_edits(edit_seed, steps);

        const SequenceOutcome outcome = run_edit_sequence(display_name, seed_text, edits);
        REQUIRE(outcome.first_mismatch.empty());
        CHECK(outcome.mismatches == 0);
        CHECK(outcome.steps == steps);
        CHECK(outcome.recomputes == outcome.changed_steps); // exactly one per real edit
        CHECK(outcome.changed_steps > 0);                   // each sequence did real work
        total.steps += outcome.steps;
        total.changed_steps += outcome.changed_steps;
        total.mismatches += outcome.mismatches;
        total.erroring_parses += outcome.erroring_parses;
        total.clean_parses += outcome.clean_parses;
        total.nonempty_ir += outcome.nonempty_ir;
        total.recomputes += outcome.recomputes;
        ++sequences;
    }

    CHECK(sequences == kSequences);
    CHECK(total.mismatches == 0);
    CHECK(total.recomputes == total.changed_steps);
    // Non-vacuity: the guard must have seen both projections (a clean parse that
    // lowers to real IR, and a parse that produced diagnostics) and it must have
    // done real work rather than replaying degenerate no-op edits.
    CHECK(total.steps >= kSequences * kMinSteps);
    CHECK(total.changed_steps > kSequences);
    CHECK(total.erroring_parses > 0);
    CHECK(total.clean_parses > 0);
    CHECK(total.nonempty_ir > 0);
    MESSAGE("edit-sequence property: " << sequences << " sequences, " << total.steps
                                       << " steps (" << total.changed_steps
                                       << " text-changing; " << total.clean_parses
                                       << " clean / " << total.erroring_parses
                                       << " erroring parses, " << total.nonempty_ir
                                       << " with non-empty IR)");
}

TEST_CASE("equal-valued edits never recompute and never change the projection") {
    // The ResetEqual edit operator is the equal-write no-op path. Isolated here
    // so a regression in it cannot hide inside a mixed sequence's aggregate
    // counters, and asserted via the engine's own revision/no-op counters.
    const auto seeds = collect_seeds();
    REQUIRE(seeds.size() >= kMinSeedFiles);
    const auto &[display_name, seed_text] = seeds.front();

    std::vector<Edit> equal_edits;
    for (std::size_t index = 0; index < 32; ++index) {
        Edit edit;
        edit.kind = ahfl::test_support::query_edit::EditKind::ResetEqual;
        equal_edits.push_back(edit);
    }

    const SequenceOutcome outcome = run_edit_sequence(display_name, seed_text, equal_edits);
    CHECK(outcome.first_mismatch.empty());
    CHECK(outcome.mismatches == 0);
    CHECK(outcome.changed_steps == 0);
    CHECK(outcome.recomputes == 0);

    // The same sequence through the production driver, asserted on the engine's
    // own bookkeeping: 32 equal writes must produce 32 registered no-ops and
    // leave both the revision clock and the compute count untouched.
    ahfl::query::FrontendQueries engine;
    const ahfl::query::FileId file{0};
    engine.set_source_text(file, display_name, seed_text);
    REQUIRE(engine.parse(file).has_value());
    const ahfl::query::Revision revision_before = engine.revision();
    const std::size_t computes_before = engine.parse_computes(file);
    for (std::size_t index = 0; index < 32; ++index) {
        engine.set_source_text(file, display_name, seed_text);
        REQUIRE(engine.parse(file).has_value());
    }
    CHECK(engine.revision() == revision_before);
    CHECK(engine.parse_computes(file) == computes_before);
    CHECK(engine.stats().input_update_noops >= 32);
}

TEST_CASE("editing one file never recomputes an untouched sibling") {
    // "Invalidates too much" is a real failure: a sibling slot whose transitive
    // inputs were untouched must not run its compute function, and the sibling's
    // projection must be the byte-identical memo the first evaluation produced.
    const auto seeds = collect_seeds();
    REQUIRE(seeds.size() >= kMinSeedFiles);

    ahfl::query::FrontendQueries engine;
    const ahfl::query::FileId file_a{0};
    const ahfl::query::FileId file_b{1};
    const std::string name_a = seeds[0].first;
    const std::string name_b = seeds[1].first;
    engine.set_source_text(file_a, name_a, seeds[0].second);
    engine.set_source_text(file_b, name_b, seeds[1].second);

    const auto first_a = engine.parse(file_a);
    const auto first_b = engine.parse(file_b);
    REQUIRE(first_a.has_value());
    REQUIRE(first_b.has_value());
    REQUIRE(engine.parse_computes(file_a) == 1);
    REQUIRE(engine.parse_computes(file_b) == 1);
    const auto sibling_before = ahfl::test_support::query_edit::project_snapshot(*first_b);

    std::string text = seeds[0].second;
    std::size_t edits_applied = 0;
    std::size_t text_changes = 0;
    for (std::size_t sequence = 0; sequence < 24; ++sequence) {
        for (const Edit &edit : seeded_edits(0xa5a5ULL + sequence, 5)) {
            const std::string next_text = ahfl::test_support::query_edit::apply_edit(text, edit);
            if (next_text != text) {
                ++text_changes;
            }
            text = next_text;
            engine.set_source_text(file_a, name_a, text);
            const auto queried = engine.parse(file_a);
            REQUIRE(queried.has_value());
            ++edits_applied;
        }
        // B's memo is untouched: same compute count, same value.
        CHECK(engine.parse_computes(file_b) == 1);
        const auto sibling_after = engine.parse(file_b);
        REQUIRE(sibling_after.has_value());
        CHECK(ahfl::test_support::query_edit::project_snapshot(*sibling_after) == sibling_before);
        CHECK(engine.parse_computes(file_b) == 1); // re-reading is a memo hit
    }

    CHECK(edits_applied == 24 * 5);
    // A's own precision is asserted here too: the long-lived slot recomputes
    // exactly once per text-changing edit, and never for a degenerate one.
    CHECK(engine.parse_computes(file_a) == text_changes + 1);
    CHECK(text_changes > 0);
}
