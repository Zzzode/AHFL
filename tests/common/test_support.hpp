#pragma once

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "runtime/evaluator/executor.hpp"

#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ahfl::test_support {

[[nodiscard]] inline bool diagnostics_contain(const DiagnosticBag &diagnostics,
                                              std::string_view needle) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(needle) != std::string::npos) {
            return true;
        }
        for (const auto &related : entry.related) {
            if (related.message.find(needle) != std::string::npos) {
                return true;
            }
        }
    }

    return false;
}

[[nodiscard]] inline std::size_t diagnostic_count_with_code(const DiagnosticBag &diagnostics,
                                                            std::string_view code) {
    std::size_t count = 0;
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == code) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] inline const Diagnostic *diagnostic_with_code(const DiagnosticBag &diagnostics,
                                                            std::string_view code) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == code) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] inline bool diagnostics_contain_code(const DiagnosticBag &diagnostics,
                                                   DiagnosticCategory category,
                                                   std::string_view code) {
    const std::string full_code = std::string(to_string(category)) + "." + std::string(code);
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == full_code) {
            return true;
        }
    }

    return false;
}

using TestCaseRunner = std::function<int(const std::span<const std::string>)>;

struct TestDispatchEntry {
    std::string_view name;
    std::size_t required_args;
    std::string_view usage;
    TestCaseRunner run;
};

[[nodiscard]] inline int dispatch_test_case(const std::string &suite_name,
                                            int argc,
                                            char **argv,
                                            std::span<const TestDispatchEntry> entries) {
    if (argc < 2) {
        std::cerr << "usage: " << suite_name << " <case> [args...]\n";
        return 2;
    }

    const std::string test_case = argv[1];
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 2 ? argc - 2 : 0));
    for (int index = 2; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }

    for (const auto &entry : entries) {
        if (test_case == entry.name) {
            if (args.size() < entry.required_args) {
                std::cerr << "usage: " << suite_name << " " << entry.name;
                if (!entry.usage.empty()) {
                    std::cerr << " " << entry.usage;
                }
                std::cerr << '\n';
                return 2;
            }
            return entry.run(std::span<const std::string>(args));
        }
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}

[[nodiscard]] inline ProjectInput workflow_value_flow_project_input(
    const std::filesystem::path &app_manifest_path) {
    const auto app_root = std::filesystem::weakly_canonical(app_manifest_path).parent_path();
    const auto fixture_root = app_root.parent_path();
    const auto repo_root = fixture_root.parent_path().parent_path().parent_path();
    ProjectInput input;
    input.entry_files.push_back(app_root / "main.ahfl");
    input.include_stdlib = false;
    input.inject_prelude = false;
    input.module_roots.push_back(ProjectInput::ModuleRoot{
        .prefix = "std",
        .root = repo_root / "std",
        .exported_modules = {"prelude", "option"},
    });
    input.module_roots.push_back(ProjectInput::ModuleRoot{
        .prefix = "app",
        .root = app_root,
        .exported_modules = {"main"},
    });
    input.module_roots.push_back(ProjectInput::ModuleRoot{
        .prefix = "lib",
        .root = fixture_root / "lib",
        .exported_modules = {"agents", "types"},
    });
    return input;
}

[[nodiscard]] inline std::optional<ir::Program>
load_project_ir(const std::filesystem::path &app_manifest_path) {
    const Frontend frontend;

    const auto project_result =
        frontend.parse_project(workflow_value_flow_project_input(app_manifest_path));
    if (project_result.has_errors()) {
        project_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const Resolver resolver;
    const auto resolve_result = resolver.resolve(project_result.graph);
    if (resolve_result.has_errors()) {
        resolve_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const TypeChecker type_checker;
    const auto type_check_result = type_checker.check(project_result.graph, resolve_result);
    if (type_check_result.has_errors()) {
        type_check_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const Validator validator;
    const auto validation_result =
        validator.validate(project_result.graph, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        validation_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return lower_program_ir(project_result.graph, resolve_result, type_check_result);
}

// ---------------------------------------------------------------------------
// Evaluator / ExecAssertFailed helpers (P4-02 structured-kind assertions)
// ---------------------------------------------------------------------------

/// If the optional ExecResult carries an ExecAssertFailed outcome, return a
/// pointer to it; otherwise nullptr.  Caller retains ownership.
[[nodiscard]] inline const evaluator::ExecAssertFailed *
extract_assert_failed(const std::optional<evaluator::ExecResult> &exec_result) {
    if (!exec_result.has_value()) return nullptr;
    return std::get_if<evaluator::ExecAssertFailed>(&exec_result->outcome);
}

/// True iff the ExecResult carries an ExecAssertFailed whose `kind` matches
/// `expected`.  Returns false when the result is missing or the outcome is
/// not an assertion failure.
[[nodiscard]] inline bool
assert_failed_kind_is(const std::optional<evaluator::ExecResult> &exec_result,
                      evaluator::AssertionKind expected) {
    const auto *af = extract_assert_failed(exec_result);
    return af != nullptr && af->kind == expected;
}

/// True iff the ExecResult carries an ExecAssertFailed whose message contains
/// the provided substring.  String-matching fallback kept alongside the kind
/// helper so tests can verify both dimensions independently.
[[nodiscard]] inline bool
assert_failed_message_contains(const std::optional<evaluator::ExecResult> &exec_result,
                               std::string_view needle) {
    const auto *af = extract_assert_failed(exec_result);
    return af != nullptr && af->message.find(needle) != std::string::npos;
}

} // namespace ahfl::test_support
