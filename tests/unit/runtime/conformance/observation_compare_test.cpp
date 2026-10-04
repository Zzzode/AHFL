// KR6.7 (RFC 0026 P7): differential comparator unit tests. Hand-rolled
// check()/main(), mirroring conformance_case_test.cpp. No engine is linked
// or invoked; the comparator is pure (parses two observation documents and
// compares five dimensions + status).
//
// Coverage:
//   (a) node_observation_matches_expectation (expectation lane): full-match
//       agreement on all five dimensions; absent capability_arguments
//       defaults to an empty array and AGREES with a capability-free
//       observation; a capability call whose envelope the expectation did
//       not bless (matching capability_sequence but absent
//       capability_arguments) DIVERGES on the argument dimension; present
//       capability_arguments agrees when matching and diverges on a tampered
//       envelope or a length mismatch; a missing capability_arguments key
//       diverges.
//   (b) observations_agree (blessing lane): identical observations agree;
//       a tampered capability_arguments envelope diverges; a renamed
//       capability_sequence element diverges; a missing capability_arguments
//       key on one side diverges.

#include "conformance/observation_compare.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

using ahfl::conformance::CaseExpectations;
using ahfl::conformance::ExpectedRunStatus;
using ahfl::conformance::node_observation_matches_expectation;
using ahfl::conformance::observations_agree;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

// A synthetic observation with one capability call. The capability_arguments
// envelope is canonical wire JSON (_type discriminator first, then fields
// lexicographically).
constexpr std::string_view kObservationOneCap = R"({
  "status":"completed",
  "state_sequence":[{"agent":"A","state":"S"}],
  "capability_sequence":["cap::Name"],
  "capability_arguments":[{"_type":"Frame","value":42}],
  "output_json":{"_type":"Out","value":"done"}
})";

// A synthetic observation with no capability calls.
constexpr std::string_view kObservationNoCaps = R"({
  "status":"completed",
  "state_sequence":[{"agent":"A","state":"S"}],
  "capability_sequence":[],
  "capability_arguments":[],
  "output_json":{"_type":"Out","value":"done"}
})";

// The same observation as kObservationOneCap but with a tampered envelope
// (an extra __mutation__ field changes the canonical bytes).
constexpr std::string_view kObservationTamperedArgs = R"({
  "status":"completed",
  "state_sequence":[{"agent":"A","state":"S"}],
  "capability_sequence":["cap::Name"],
  "capability_arguments":[{"_type":"Frame","__mutation__":"mutated","value":42}],
  "output_json":{"_type":"Out","value":"done"}
})";

// The same observation as kObservationOneCap but with a renamed capability.
constexpr std::string_view kObservationRenamedCap = R"({
  "status":"completed",
  "state_sequence":[{"agent":"A","state":"S"}],
  "capability_sequence":["mutated::unexpected::capability"],
  "capability_arguments":[{"_type":"Frame","value":42}],
  "output_json":{"_type":"Out","value":"done"}
})";

// An observation missing the capability_arguments key entirely.
constexpr std::string_view kObservationMissingArgs = R"({
  "status":"completed",
  "state_sequence":[{"agent":"A","state":"S"}],
  "capability_sequence":["cap::Name"],
  "output_json":{"_type":"Out","value":"done"}
})";

[[nodiscard]] CaseExpectations make_expectation_with_caps() {
    CaseExpectations expect;
    expect.run_status = ExpectedRunStatus::Completed;
    expect.state_sequence = {"S"};
    expect.capability_sequence = {"cap::Name"};
    expect.capability_arguments = R"([{"_type":"Frame","value":42}])";
    expect.output_json = R"({"_type":"Out","value":"done"})";
    return expect;
}

[[nodiscard]] CaseExpectations make_expectation_no_caps() {
    CaseExpectations expect;
    expect.run_status = ExpectedRunStatus::Completed;
    expect.state_sequence = {"S"};
    expect.capability_sequence = {};
    // capability_arguments intentionally absent (nullopt).
    expect.output_json = R"({"_type":"Out","value":"done"})";
    return expect;
}

void test_expectation_lane_full_match() {
    const auto expect = make_expectation_with_caps();
    const auto divergence =
        node_observation_matches_expectation(expect, kObservationOneCap);
    check(!divergence.has_value(),
          "expectation lane: full match on all five dimensions agrees");
    if (divergence.has_value()) {
        std::cerr << "  unexpected divergence: " << *divergence << "\n";
    }
}

void test_expectation_lane_absent_args_empty_obs_agrees() {
    const auto expect = make_expectation_no_caps();
    const auto divergence =
        node_observation_matches_expectation(expect, kObservationNoCaps);
    check(!divergence.has_value(),
          "expectation lane: absent capability_arguments defaults to [] and agrees with a capability-free observation");
    if (divergence.has_value()) {
        std::cerr << "  unexpected divergence: " << *divergence << "\n";
    }
}

void test_expectation_lane_absent_args_nonempty_obs_diverges() {
    // The expectation lists the capability call (so capability_sequence
    // matches) but omits capability_arguments. The comparator defaults the
    // absent field to an empty array and catches the length divergence:
    // the observation recorded an envelope the expectation did not bless.
    auto expect = make_expectation_with_caps();
    expect.capability_arguments = std::nullopt;
    const auto divergence =
        node_observation_matches_expectation(expect, kObservationOneCap);
    check(divergence.has_value(),
          "expectation lane: absent capability_arguments diverges on a capability-bearing observation (unexpected argument guard)");
    if (divergence.has_value()) {
        const std::string &reason = *divergence;
        check(reason.find("capability_arguments") != std::string::npos,
              "expectation lane: divergence reason names the capability_arguments dimension");
    }
}

void test_expectation_lane_present_args_matching_agrees() {
    const auto expect = make_expectation_with_caps();
    const auto divergence =
        node_observation_matches_expectation(expect, kObservationOneCap);
    check(!divergence.has_value(),
          "expectation lane: present capability_arguments matching the observation agrees");
}

void test_expectation_lane_tampered_envelope_diverges() {
    const auto expect = make_expectation_with_caps();
    const auto divergence =
        node_observation_matches_expectation(expect, kObservationTamperedArgs);
    check(divergence.has_value(),
          "expectation lane: tampered capability_arguments envelope diverges");
    if (divergence.has_value()) {
        const std::string &reason = *divergence;
        check(reason.find("capability_arguments[0]") != std::string::npos,
              "expectation lane: divergence reason pinpoints envelope[0]");
    }
}

void test_expectation_lane_length_mismatch_diverges() {
    auto expect = make_expectation_with_caps();
    // Two envelopes in the expectation but only one capability call in the
    // observation.
    expect.capability_arguments =
        R"([{"_type":"Frame","value":42},{"_type":"Frame","value":43}])";
    const auto divergence =
        node_observation_matches_expectation(expect, kObservationOneCap);
    check(divergence.has_value(),
          "expectation lane: capability_arguments length mismatch diverges");
    if (divergence.has_value()) {
        const std::string &reason = *divergence;
        check(reason.find("length diverged") != std::string::npos,
              "expectation lane: divergence reason reports length mismatch");
    }
}

void test_expectation_lane_missing_key_diverges() {
    const auto expect = make_expectation_with_caps();
    const auto divergence =
        node_observation_matches_expectation(expect, kObservationMissingArgs);
    check(divergence.has_value(),
          "expectation lane: missing capability_arguments key diverges");
    if (divergence.has_value()) {
        const std::string &reason = *divergence;
        check(reason.find("missing the capability_arguments array") !=
                  std::string::npos,
              "expectation lane: divergence reason reports the missing key");
    }
}

void test_blessing_lane_identical_agrees() {
    const auto divergence =
        observations_agree(kObservationOneCap, kObservationOneCap);
    check(!divergence.has_value(),
          "blessing lane: identical observations agree");
    if (divergence.has_value()) {
        std::cerr << "  unexpected divergence: " << *divergence << "\n";
    }
}

void test_blessing_lane_tampered_args_diverges() {
    const auto divergence =
        observations_agree(kObservationOneCap, kObservationTamperedArgs);
    check(divergence.has_value(),
          "blessing lane: tampered capability_arguments envelope diverges");
    if (divergence.has_value()) {
        const std::string &reason = *divergence;
        check(reason.find("capability_arguments[0]") != std::string::npos,
              "blessing lane: divergence reason pinpoints envelope[0]");
    }
}

void test_blessing_lane_renamed_cap_diverges() {
    const auto divergence =
        observations_agree(kObservationOneCap, kObservationRenamedCap);
    check(divergence.has_value(),
          "blessing lane: renamed capability_sequence element diverges");
    if (divergence.has_value()) {
        const std::string &reason = *divergence;
        check(reason.find("capability_sequence[0]") != std::string::npos,
              "blessing lane: divergence reason pinpoints sequence[0]");
    }
}

void test_blessing_lane_missing_args_diverges() {
    const auto divergence =
        observations_agree(kObservationOneCap, kObservationMissingArgs);
    check(divergence.has_value(),
          "blessing lane: missing capability_arguments on one side diverges");
    if (divergence.has_value()) {
        const std::string &reason = *divergence;
        check(reason.find("missing the capability_arguments array") !=
                  std::string::npos,
              "blessing lane: divergence reason reports the missing key");
    }
}

} // namespace

int main() {
    test_expectation_lane_full_match();
    test_expectation_lane_absent_args_empty_obs_agrees();
    test_expectation_lane_absent_args_nonempty_obs_diverges();
    test_expectation_lane_present_args_matching_agrees();
    test_expectation_lane_tampered_envelope_diverges();
    test_expectation_lane_length_mismatch_diverges();
    test_expectation_lane_missing_key_diverges();
    test_blessing_lane_identical_agrees();
    test_blessing_lane_tampered_args_diverges();
    test_blessing_lane_renamed_cap_diverges();
    test_blessing_lane_missing_args_diverges();

    if (g_failures == 0) {
        std::cout << "observation_compare_test: all tests passed\n";
    }
    return g_failures == 0 ? 0 : 1;
}
