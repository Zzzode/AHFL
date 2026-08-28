#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// KR6.4 final acceptance: an AHFL-IR -> Core-IR lowering regression driven
// through the REAL sysroot (parse_project + repo std), not hand-built IR. It
// proves the production stdlib symbol-identity path works end to end:
//   * a capability call nested inside `std::option::Option::Some(...)` survives
//     as an ordered CapabilityCall inside its branch region (no dropped effect);
//   * `Option::Some` resolves to the builtin Option type id + variant 0, and a
//     UNIT `Option::None` resolves to the SAME type id + variant 1;
//   * a unit user-enum variant gets typed identity;
//   * a user-defined `Option` coexists with `std::option::Option` WITHOUT the
//     two variant tables cross-contaminating (distinct CoreTypeIds);
//   * the only Core errors are the deliberately-deferred field projections.

namespace {

using namespace ahfl;

void write_file(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

[[nodiscard]] std::filesystem::path make_temp_project(std::string_view name) {
    const auto root = std::filesystem::temp_directory_path() / ("ahfl_core_ir_" + std::string(name));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

// Recursively collect all capability-call statements in a region and note, for
// each, whether it was reached inside an `if` branch (depth > 0).
struct FoundCall {
    const ir::core::CoreCapabilityCallStmt *stmt{nullptr};
    int depth{0};
};
void collect_calls_with_depth(const ir::core::CoreRegion &region, int depth,
                              std::vector<FoundCall> &out) {
    for (const auto &stmt : region.statements) {
        std::visit(
            [&](const auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ir::core::CoreCapabilityCallStmt>) {
                    out.push_back(FoundCall{&node, depth});
                } else if constexpr (std::is_same_v<T, ir::core::CoreIfStmt>) {
                    if (node.then_region) {
                        collect_calls_with_depth(*node.then_region, depth + 1, out);
                    }
                    if (node.else_region) {
                        collect_calls_with_depth(*node.else_region, depth + 1, out);
                    }
                }
            },
            stmt.node);
    }
}

} // namespace

TEST_CASE("KR6.4 e2e: Core-IR lowering over the real sysroot preserves stdlib symbol identity") {
    const auto root = make_temp_project("stdlib_identity");
    const auto main_path = root / "app" / "main.ahfl";

    // A program that exercises, through the real std sysroot:
    //   * a capability call nested in std Option::Some inside an if-branch;
    //   * std Option::None (unit variant);
    //   * a user unit-enum variant (Verdict::Approve);
    //   * a user-defined `Option` type that must NOT hijack std::option::Option.
    const std::string source = R"AHFL(
module app::main;

import std::option;

enum Verdict {
    Approve,
    Reject,
}

// A user-defined Option that shadows the NAME but is a DISTINCT nominal type.
enum Option {
    Present,
    Absent,
}

struct Request {
    amount: Int;
}

struct Ctx {
    slot: std::option::Option<Bool> = std::option::Option::None;
    verdict: Verdict = Verdict::Reject;
    mine: Option = Option::Absent;
}

struct Reply {
    ok: Bool = false;
}

capability Charge(amount: Int) -> Bool;

agent Payer {
    input: Request;
    context: Ctx;
    output: Reply;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Charge];

    transition Init -> Done;
}

flow for Payer {
    state Init {
        if input.amount > 0 {
            ctx.slot = std::option::Option::Some(Charge(input.amount));
            ctx.verdict = Verdict::Approve;
            ctx.mine = Option::Present;
        } else {
            ctx.slot = std::option::Option::None;
        }
        goto Done;
    }

    state Done {
        return Reply { ok: false };
    }
}
)AHFL";
    write_file(main_path, source);

    const Frontend frontend;
    const auto parse_result = parse_project(
        frontend,
        test_support::project_input_with_repo_std_for_test_file(main_path, root, __FILE__));
    REQUIRE_FALSE(parse_result.has_errors());

    const Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    REQUIRE_FALSE(resolve_result.has_errors());

    const TypeChecker checker;
    const auto type_result = checker.check(parse_result.graph, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto ahfl_ir = lower_program_ir(parse_result.graph, resolve_result, type_result);
    const auto result = ir::core::lower_ahfl_to_core(ahfl_ir);

    // Only the deferred field-projection diagnostics are allowed; NO dropped
    // effect / unresolved capability / unresolved type/variant.
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " — " << d.message);
        CHECK(d.code == ir::core::diag::kUnloweredFieldProjection);
    }

    // Locate the Payer flow.
    const ir::core::CoreFlowDecl *flow = nullptr;
    for (const auto &f : result.program.flows) {
        if (f.agent_name == "Payer" || f.target_ref.local_name == "Payer") {
            flow = &f;
        }
    }
    REQUIRE(flow != nullptr);
    // The typed target identity is set (Principle 2), not left invalid.
    CHECK(flow->target.value != ir::core::CoreAgentId::kInvalid);

    // Init handler: the Charge call must be INSIDE the then-branch (depth 1),
    // proving nested-in-Some + branch-region preservation together.
    const ir::core::CoreFlowState *init = nullptr;
    for (const auto &s : flow->states) {
        if (s.state_name == "Init") {
            init = &s;
        }
    }
    REQUIRE(init != nullptr);
    std::vector<FoundCall> calls;
    collect_calls_with_depth(init->body, 0, calls);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].stmt->callee_name.find("Charge") != std::string::npos);
    CHECK(calls[0].depth == 1); // inside the if then-region

    // Resolve the std Option type id (from the type table) to compare against.
    std::optional<ir::core::CoreTypeId> std_option_id;
    std::optional<ir::core::CoreTypeId> user_option_id;
    for (std::uint32_t i = 0; i < result.program.types.size(); ++i) {
        const auto &t = result.program.types[i];
        if (t.name == "std::option::Option") {
            std_option_id = ir::core::CoreTypeId{i};
        }
        if (t.name == "app::main::Option" || t.name == "Option") {
            user_option_id = ir::core::CoreTypeId{i};
        }
    }
    REQUIRE(std_option_id.has_value());
    REQUIRE(user_option_id.has_value());
    // The user Option and std Option are DISTINCT nominal types (no hijack).
    CHECK(std_option_id->value != user_option_id->value);

    // Walk every construct/qualified expr and check typed identities.
    bool saw_some = false;
    bool saw_none = false;
    bool saw_verdict_approve = false;
    bool saw_user_present = false;
    for (const auto &expr : flow->exprs) {
        if (const auto *ctor = std::get_if<ir::core::CoreConstructExpr>(&expr.node)) {
            if (ctor->is_enum_variant && ctor->variant_name == "Some") {
                saw_some = true;
                CHECK(ctor->resolved);
                CHECK(ctor->type_id == *std_option_id);   // owning type = std Option
                CHECK(ctor->variant.value == 0u);         // Some = 0
            }
        }
        if (const auto *q = std::get_if<ir::core::CoreQualifiedExpr>(&expr.node)) {
            if (q->name.find("Option::None") != std::string::npos) {
                saw_none = true;
                CHECK(q->resolved);
                CHECK(q->type_id == *std_option_id);       // SAME type id as Some
                CHECK(q->variant.value == 1u);             // None = 1
            }
            if (q->name.find("Verdict::Approve") != std::string::npos) {
                saw_verdict_approve = true;
                CHECK(q->resolved);
            }
            if (q->name.find("Option::Present") != std::string::npos) {
                saw_user_present = true;
                CHECK(q->resolved);
                CHECK(q->type_id == *user_option_id);      // resolves to USER Option
            }
        }
    }
    CHECK(saw_some);
    CHECK(saw_none);
    CHECK(saw_verdict_approve);
    CHECK(saw_user_present);
}
