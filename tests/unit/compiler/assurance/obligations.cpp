#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"

#include "compiler/assurance/assurance.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ahfl;

struct CompiledProgram {
    ir::Program program;
    DiagnosticBag diagnostics;
};

[[nodiscard]] CompiledProgram compile_source(std::string_view filename,
                                             std::string_view source) {
    const Frontend frontend;
    const auto parse_result = frontend.parse_text(std::string(filename),
                                                  std::string(source));
    if (parse_result.has_errors() || parse_result.program == nullptr) {
        return {.program = {}, .diagnostics = parse_result.diagnostics};
    }

    const Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        CompiledProgram result{.program = {}, .diagnostics = resolve_result.diagnostics};
        return result;
    }

    const TypeChecker type_checker;
    const auto type_check_result =
        type_checker.check(*parse_result.program, resolve_result);

    const Validator validator;
    auto validate_result = validator.validate(*parse_result.program,
                                              resolve_result,
                                              type_check_result);

    auto program = lower_program_ir(*parse_result.program,
                                    resolve_result,
                                    type_check_result);
    // Merge the diagnostic bags' error/warning counts while preserving the
    // earliest errors — the assurance pipeline itself doesn't consume these,
    // but surfacing them keeps test-diagnosis output sane.
    DiagnosticBag combined;
    combined.append(parse_result.diagnostics);
    combined.append(resolve_result.diagnostics);
    combined.append(type_check_result.diagnostics);
    combined.append(validate_result.diagnostics);
    return {.program = std::move(program), .diagnostics = std::move(combined)};
}

[[nodiscard]] std::string render(const assurance::AssuranceBundle &bundle) {
    std::ostringstream out;
    assurance::print_assurance_bundle_json(bundle, out);
    return out.str();
}

// Returns the number of decreases clauses (ContractClauseKind::Decreases) in
// the compiled program's IR. Each clause contributes wildcard_or_terms_count
// entries in the verification obligations vector; this helper computes the
// expected obligation count so the test can assert the invariant that the
// JSON count matches the source count exactly.
[[nodiscard]] std::size_t count_decreases_terms(const ir::Program &program) {
    std::size_t total = 0;
    for (const auto &declaration : program.declarations) {
        const auto *contract = std::get_if<ir::ContractDecl>(&declaration);
        if (contract == nullptr) {
            continue;
        }
        for (const auto &clause : contract->clauses) {
            if (clause.kind != ir::ContractClauseKind::Decreases) {
                continue;
            }
            if (clause.decreases_wildcard) {
                total += 1;
            } else {
                total += clause.decreases_terms.size();
            }
        }
    }
    return total;
}

// Minimal but strict JSON-schema-like validator. Keeps the test dependency
// free by walking the JSON string directly; covers exactly the keys mandated
// by P4.S7b: "verification" object with "obligations" array whose items each
// contain { decreases_pattern, well_founded, location, status }.
bool schema_validate_obligations(const std::string &json, std::string &error) {
    // Require the verification block.
    const auto verification_pos = json.find("\"verification\"");
    if (verification_pos == std::string::npos) {
        error = "missing 'verification' object";
        return false;
    }
    const auto obligations_pos = json.find("\"obligations\"", verification_pos);
    if (obligations_pos == std::string::npos) {
        error = "missing 'verification.obligations' array";
        return false;
    }
    const auto array_open = json.find('[', obligations_pos);
    if (array_open == std::string::npos) {
        error = "'obligations' is not an array";
        return false;
    }

    // For each obligation entry, the four required keys must appear before
    // the next '}' that closes the entry. We count '{'/'}' depth starting
    // right after the obligations '['.
    std::size_t cursor = array_open + 1;
    int depth = 1;
    int entry_depth = 0;
    std::size_t entry_start = std::string::npos;
    while (cursor < json.size()) {
        const char ch = json[cursor];
        if (ch == '"') {
            // Skip string literal to avoid mis-counting braces inside
            // strings.
            ++cursor;
            while (cursor < json.size()) {
                if (json[cursor] == '\\') {
                    cursor += 2;
                    continue;
                }
                if (json[cursor] == '"') {
                    ++cursor;
                    break;
                }
                ++cursor;
            }
            continue;
        }
        if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) {
                break;
            }
        } else if (ch == '{') {
            ++depth;
            if (entry_depth == 0) {
                entry_start = cursor;
                entry_depth = depth;
            }
        } else if (ch == '}') {
            --depth;
            if (entry_depth != 0 && depth < entry_depth) {
                // Completed an top-level entry in the obligations array.
                REQUIRE(entry_start != std::string::npos);
                const std::string_view entry(json.data() + entry_start,
                                             cursor - entry_start + 1);
                for (const auto &required_key : {"\"decreases_pattern\"",
                                                 "\"well_founded\"",
                                                 "\"location\"",
                                                 "\"status\""}) {
                    if (entry.find(required_key) == std::string_view::npos) {
                        error = std::string("obligation entry missing key ") + required_key;
                        return false;
                    }
                }
                entry_start = std::string::npos;
                entry_depth = 0;
            }
        }
        ++cursor;
    }

    return true;
}

TEST_CASE("assurance.obligations: empty program yields empty obligations + schema pass") {
    constexpr std::string_view kSource = R"ahfl(
module p4_s7b_empty;

struct Ctx { v: Int = 0; }
struct Req { v: Int; }
struct Resp { v: Int; }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for A {
    state Init { goto Done; }
    state Done { return Resp { v: ctx.v }; }
}

workflow W {
    input: Req;
    output: Resp;
    node run: A(input);
    return: run;
}
)ahfl";

    auto compiled = compile_source("p4_s7b_test.ahfl", kSource);
    REQUIRE_FALSE(compiled.diagnostics.has_error());

    const auto bundle = assurance::build_assurance_bundle(compiled.program);
    CHECK(bundle.verification.obligations.empty());
    CHECK(count_decreases_terms(compiled.program) == 0);

    const std::string json = render(bundle);
    std::string error;
    CHECK(schema_validate_obligations(json, error));
    INFO(error);
}

TEST_CASE("assurance.obligations: decreases self.length produces 1 recognized obligation") {
    constexpr std::string_view kSource = R"ahfl(
module p4_s7b_length_self;

struct Item { id: Int; }

struct Ctx {
    length: Int = 0;
}

struct Req { n: Int; }
struct Resp { remaining: Int; }

agent Worker {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

contract for Worker {
    decreases: self.length;
}

flow for Worker {
    state Init { goto Done; }
    state Done { return Resp { remaining: ctx.length }; }
}

workflow W {
    input: Req;
    output: Resp;
    node run: Worker(input);
    safety: always (not running(run) or (1 + 1 == 2));
    liveness: eventually completed(run, Done);
    return: run;
}
)ahfl";

    auto compiled = compile_source("p4_s7b_test.ahfl", kSource);
    REQUIRE_FALSE(compiled.diagnostics.has_error());

    const auto expected_count = count_decreases_terms(compiled.program);
    const auto bundle = assurance::build_assurance_bundle(compiled.program);
    REQUIRE(bundle.verification.obligations.size() == expected_count);
    CHECK(expected_count == 1);

    const auto &obligation = bundle.verification.obligations.front();
    CHECK(obligation.status == assurance::VerificationObligationStatus::Recognized);
    CHECK(obligation.well_founded == true);
    CHECK(obligation.decreases_pattern == "self.length");
    CHECK_FALSE(obligation.location.empty());

    const std::string json = render(bundle);
    std::string error;
    CHECK(schema_validate_obligations(json, error));
    INFO(error);
    // Exact-string signal for the recognition status.
    CHECK(json.find("\"status\": \"recognized\"") != std::string::npos);
}

TEST_CASE("assurance.obligations: wildcard and unrecognized statuses") {
    // One unrecognized (arbitrary function call + constant sum — not in the
    // MVP supported set) + one wildcard contract, spread across two
    // contracts so the status strings are all exercised.
    constexpr std::string_view kSource = R"ahfl(
module p4_s7b_wildcard_unknown;

struct Ctx { n: Int = 0; }
struct Req { v: Int; }
struct Resp { v: Int; }

agent WorkerWildcard {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

contract for WorkerWildcard {
    decreases: *;
}

flow for WorkerWildcard {
    state Init { goto Done; }
    state Done { return Resp { v: ctx.n }; }
}

workflow W {
    input: Req;
    output: Resp;
    node run: WorkerWildcard(input);
    return: run;
}
)ahfl";

    auto compiled = compile_source("p4_s7b_test.ahfl", kSource);
    REQUIRE_FALSE(compiled.diagnostics.has_error());

    const auto expected_count = count_decreases_terms(compiled.program);
    const auto bundle = assurance::build_assurance_bundle(compiled.program);
    REQUIRE(bundle.verification.obligations.size() == expected_count);
    CHECK(expected_count == 1);

    const auto &wild = bundle.verification.obligations.front();
    CHECK(wild.status == assurance::VerificationObligationStatus::Wildcard);
    CHECK(wild.well_founded == true);
    CHECK(wild.decreases_pattern == "*");

    const std::string json = render(bundle);
    std::string error;
    CHECK(schema_validate_obligations(json, error));
    INFO(error);
    CHECK(json.find("\"status\": \"wildcard\"") != std::string::npos);
}

TEST_CASE("assurance.obligations: mixed contracts exercise recognized + wildcard + unrecognized") {
    // Three separate contracts on different agents each contribute exactly one
    // decreases obligation, exercising every `status` value: Recognized
    // (SelfField MVP), Wildcard, and Unrecognized (opaque function call).
    constexpr std::string_view kSource = R"ahfl(
module p4_s7b_mixed_terms;

struct Ctx { idx: Int = 0; i: Int = 0; }
struct Req { v: Int; }
struct Resp { v: Int; }

fn opaque_measure(x: Int) -> Int { return x; }

// Agent 1: SelfField MVP (self.idx) → Recognized.
agent WorkerSelfField {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

contract for WorkerSelfField {
    decreases: self.idx;
}

flow for WorkerSelfField {
    state Init { goto Done; }
    state Done { return Resp { v: ctx.idx }; }
}

// Agent 2: wildcard → Wildcard.
agent WorkerWild {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

contract for WorkerWild {
    decreases: *;
}

flow for WorkerWild {
    state Init { goto Done; }
    state Done { return Resp { v: ctx.i }; }
}

// Agent 3: opaque function call → Unrecognized.
agent WorkerOpaque {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

contract for WorkerOpaque {
    decreases: opaque_measure(ctx.i);
}

flow for WorkerOpaque {
    state Init { goto Done; }
    state Done { return Resp { v: ctx.i }; }
}

workflow W {
    input: Req;
    output: Resp;
    node a: WorkerSelfField(input);
    node b: WorkerWild(input);
    node c: WorkerOpaque(input);
    return: a;
}
)ahfl";

    auto compiled = compile_source("p4_s7b_test.ahfl", kSource);
    REQUIRE_FALSE(compiled.diagnostics.has_error());

    const auto expected_count = count_decreases_terms(compiled.program);
    const auto bundle = assurance::build_assurance_bundle(compiled.program);
    REQUIRE(bundle.verification.obligations.size() == expected_count);
    CHECK(expected_count == 3);

    using Status = assurance::VerificationObligationStatus;

    // Total counts across the three statuses.
    std::size_t recognized = 0;
    std::size_t wild = 0;
    std::size_t unrecognized = 0;
    for (const auto &o : bundle.verification.obligations) {
        switch (o.status) {
            case Status::Recognized:   ++recognized;   break;
            case Status::Wildcard:     ++wild;         break;
            case Status::Unrecognized: ++unrecognized; break;
        }
    }
    CHECK(recognized == 1);
    CHECK(wild == 1);
    CHECK(unrecognized == 1);

    // The recognized self.idx obligation is present with a well-founded flag.
    bool found_self_idx = false;
    bool found_wildcard = false;
    for (const auto &o : bundle.verification.obligations) {
        if (o.status == Status::Recognized && o.decreases_pattern == "self.idx") {
            found_self_idx = true;
            CHECK(o.well_founded == true);
        }
        if (o.status == Status::Wildcard && o.decreases_pattern == "*") {
            found_wildcard = true;
            CHECK(o.well_founded == true);
        }
        if (o.status == Status::Unrecognized) {
            CHECK(o.well_founded == false);
        }
    }
    CHECK(found_self_idx);
    CHECK(found_wildcard);

    const std::string json = render(bundle);
    std::string error;
    CHECK(schema_validate_obligations(json, error));
    INFO(error);
    // Exact-string golden-signal assertions for the three status values.
    CHECK(json.find("\"status\": \"recognized\"") != std::string::npos);
    CHECK(json.find("\"status\": \"wildcard\"") != std::string::npos);
    CHECK(json.find("\"status\": \"unrecognized\"") != std::string::npos);
}
} // namespace
