#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "runtime/evaluator/value.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace {

using ahfl::test_support::project_input_with_repo_std;

void print_diagnostics(const ahfl::DiagnosticBag &diagnostics) {
    diagnostics.render(std::cout);
}

void print_diagnostics(const std::vector<ahfl::package_graph::Diagnostic> &diagnostics) {
    ahfl::test_support::print_package_graph_diagnostics(diagnostics, std::cout);
}

[[nodiscard]] std::string render_diagnostics(const ahfl::DiagnosticBag &diagnostics) {
    std::ostringstream out;
    diagnostics.render(out);
    return out.str();
}

[[nodiscard]] bool contains_text(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

[[nodiscard]] bool contains_code(const ahfl::DiagnosticBag &diagnostics, std::string_view code) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == code) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool write_text_file(const std::filesystem::path &path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << text;
    return static_cast<bool>(out);
}

[[nodiscard]] std::optional<std::string> find_first_let_type(const ahfl::ir::Program &program,
                                                             std::string_view flow_target) {
    for (const auto &declaration : program.declarations) {
        const auto *flow = std::get_if<ahfl::ir::FlowDecl>(&declaration);
        if (flow == nullptr || ahfl::ir::symbol_canonical_name(flow->target_ref) != flow_target) {
            continue;
        }

        for (const auto &handler : flow->state_handlers) {
            for (const auto &statement : handler.body.statements) {
                const auto *let_statement = std::get_if<ahfl::ir::LetStatement>(&statement->node);
                if (let_statement != nullptr) {
                    return std::string(
                        ahfl::ir::type_canonical_name(let_statement->type_ref, "Any"));
                }
            }
        }
    }

    return std::nullopt;
}

int run_ok_cross_file(const std::filesystem::path &entry, const std::filesystem::path &root) {
    auto input = ahfl::test_support::project_input_from_workspace(
        root, "check-ok-app", entry, ahfl::test_support::repo_root_from_integration_root(root));
    if (input.has_errors()) {
        print_diagnostics(input.diagnostics);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto parse_result = ahfl::parse_project(frontend, *input.input);
    if (parse_result.has_errors()) {
        print_diagnostics(parse_result.diagnostics);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        print_diagnostics(resolve_result.diagnostics);
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    const auto type_check_result = type_checker.check(parse_result.graph, resolve_result);
    if (type_check_result.has_errors()) {
        print_diagnostics(type_check_result.diagnostics);
        return 1;
    }

    const ahfl::Validator validator;
    const auto validation_result =
        validator.validate(parse_result.graph, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        print_diagnostics(validation_result.diagnostics);
        return 1;
    }

    return 0;
}

int run_fail_node_input(const std::filesystem::path &entry, const std::filesystem::path &root) {
    auto input = ahfl::test_support::project_input_from_workspace(
        root,
        "check-fail-input-app",
        entry,
        ahfl::test_support::repo_root_from_integration_root(root));
    if (input.has_errors()) {
        print_diagnostics(input.diagnostics);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto parse_result = ahfl::parse_project(frontend, *input.input);
    if (parse_result.has_errors()) {
        print_diagnostics(parse_result.diagnostics);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        print_diagnostics(resolve_result.diagnostics);
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    const auto type_check_result = type_checker.check(parse_result.graph, resolve_result);
    if (!type_check_result.has_errors()) {
        std::cerr << "expected typecheck failure\n";
        return 1;
    }

    const auto rendered = render_diagnostics(type_check_result.diagnostics);
    if (!contains_text(rendered, "exact schema mismatch in workflow node input") ||
        !contains_text(rendered, "check_fail_input/app/main.ahfl")) {
        print_diagnostics(type_check_result.diagnostics);
        return 1;
    }

    return 0;
}

int run_fail_completed_state(const std::filesystem::path &entry,
                             const std::filesystem::path &root) {
    auto input = ahfl::test_support::project_input_from_workspace(
        root,
        "check-fail-state-app",
        entry,
        ahfl::test_support::repo_root_from_integration_root(root));
    if (input.has_errors()) {
        print_diagnostics(input.diagnostics);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto parse_result = ahfl::parse_project(frontend, *input.input);
    if (parse_result.has_errors()) {
        print_diagnostics(parse_result.diagnostics);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        print_diagnostics(resolve_result.diagnostics);
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    const auto type_check_result = type_checker.check(parse_result.graph, resolve_result);
    if (type_check_result.has_errors()) {
        print_diagnostics(type_check_result.diagnostics);
        return 1;
    }

    const ahfl::Validator validator;
    const auto validation_result =
        validator.validate(parse_result.graph, resolve_result, type_check_result);
    if (!validation_result.has_errors()) {
        std::cerr << "expected validation failure\n";
        return 1;
    }

    const auto rendered = render_diagnostics(validation_result.diagnostics);
    if (!contains_text(rendered, "is not a final state of node 'run'") ||
        !contains_text(rendered, "check_fail_state/app/main.ahfl")) {
        print_diagnostics(validation_result.diagnostics);
        return 1;
    }

    return 0;
}

int run_ok_expression_type_isolated(const std::filesystem::path &entry,
                                    const std::filesystem::path &root) {
    auto input = ahfl::test_support::project_input_from_workspace(
        root,
        "expression-type-isolated-app",
        entry,
        ahfl::test_support::repo_root_from_integration_root(root));
    if (input.has_errors()) {
        print_diagnostics(input.diagnostics);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto parse_result = ahfl::parse_project(frontend, *input.input);
    if (parse_result.has_errors()) {
        print_diagnostics(parse_result.diagnostics);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        print_diagnostics(resolve_result.diagnostics);
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    const auto type_check_result = type_checker.check(parse_result.graph, resolve_result);
    if (type_check_result.has_errors()) {
        print_diagnostics(type_check_result.diagnostics);
        return 1;
    }

    const ahfl::Validator validator;
    const auto validation_result =
        validator.validate(parse_result.graph, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        print_diagnostics(validation_result.diagnostics);
        return 1;
    }

    const auto ir_program =
        ahfl::lower_program_ir(parse_result.graph, resolve_result, type_check_result);
    const auto one_reply_type = find_first_let_type(ir_program, "lib::one::OneAgent");
    const auto two_reply_type = find_first_let_type(ir_program, "lib::two::TwoAgent");

    if (!one_reply_type.has_value() || *one_reply_type != "lib::types::OneRes" ||
        !two_reply_type.has_value() || *two_reply_type != "lib::types::TwoRes") {
        std::cerr << "unexpected let type isolation result\n";
        if (one_reply_type.has_value()) {
            std::cerr << "  lib::one::OneAgent -> " << *one_reply_type << '\n';
        }
        if (two_reply_type.has_value()) {
            std::cerr << "  lib::two::TwoAgent -> " << *two_reply_type << '\n';
        }
        return 1;
    }

    return 0;
}

int run_ok_stdlib_runtime_api(const std::filesystem::path &entry,
                              const std::filesystem::path &root) {
    auto input = ahfl::test_support::project_input_from_manifest(
        root / "app" / "ahfl.toml",
        entry,
        ahfl::test_support::repo_root_from_integration_root(root));
    if (input.has_errors()) {
        print_diagnostics(input.diagnostics);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto parse_result = ahfl::parse_project(frontend, *input.input);
    if (parse_result.has_errors()) {
        print_diagnostics(parse_result.diagnostics);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        print_diagnostics(resolve_result.diagnostics);
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    const auto type_check_result = type_checker.check(parse_result.graph, resolve_result);
    if (type_check_result.has_errors()) {
        print_diagnostics(type_check_result.diagnostics);
        return 1;
    }

    const ahfl::Validator validator;
    const auto validation_result =
        validator.validate(parse_result.graph, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        print_diagnostics(validation_result.diagnostics);
        return 1;
    }

    const auto ir_program =
        ahfl::lower_program_ir(parse_result.graph, resolve_result, type_check_result);
    const auto call_eval = ahfl::evaluator::make_program_call_eval(ir_program);
    const auto expect_int = [&](std::string function_name, std::int64_t expected) {
        const ahfl::ir::Expr call_expr{
            .node = ahfl::ir::CallExpr{.callee = std::move(function_name), .arguments = {}},
            .source_range = std::nullopt,
            .resolved_type = {},
        };
        const auto result =
            ahfl::evaluator::eval_expr(call_expr, ahfl::evaluator::EvalContext{}, call_eval);
        if (result.has_errors()) {
            print_diagnostics(result.diagnostics);
            return false;
        }
        const auto *value = std::get_if<ahfl::evaluator::IntValue>(&result.value.node);
        if (value == nullptr || value->value != expected) {
            std::cerr << "unexpected runtime result for function: expected " << expected;
            if (value != nullptr) {
                std::cerr << ", got " << value->value;
            } else {
                std::cerr << ", got non-Int value";
            }
            std::cerr << '\n';
            return false;
        }
        return true;
    };

    if (!expect_int("app::main::runtime_collections_score", 26)) {
        return 1;
    }
    if (!expect_int("app::main::runtime_option_result_score", 15)) {
        return 1;
    }

    return 0;
}

int run_ok_trait_runtime_dispatch(const std::filesystem::path &entry,
                                  const std::filesystem::path &root) {
    auto input = ahfl::test_support::project_input_from_workspace(
        root,
        "trait-runtime-smoke",
        entry,
        ahfl::test_support::repo_root_from_integration_root(root));
    if (input.has_errors()) {
        print_diagnostics(input.diagnostics);
        return 1;
    }

    const ahfl::Frontend frontend;
    const auto parse_result = ahfl::parse_project(frontend, *input.input);
    if (parse_result.has_errors()) {
        print_diagnostics(parse_result.diagnostics);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        print_diagnostics(resolve_result.diagnostics);
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    const auto type_check_result = type_checker.check(parse_result.graph, resolve_result);
    if (type_check_result.has_errors()) {
        print_diagnostics(type_check_result.diagnostics);
        return 1;
    }

    const ahfl::Validator validator;
    const auto validation_result =
        validator.validate(parse_result.graph, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        print_diagnostics(validation_result.diagnostics);
        return 1;
    }

    const auto ir_program =
        ahfl::lower_program_ir(parse_result.graph, resolve_result, type_check_result);
    const auto call_eval = ahfl::evaluator::make_program_call_eval(ir_program);
    const ahfl::ir::Expr call_expr{
        .node =
            ahfl::ir::CallExpr{
                .callee = "app::main::runtime_trait_dispatch_score",
                .arguments = {},
            },
        .source_range = std::nullopt,
        .resolved_type = {},
    };
    const auto result =
        ahfl::evaluator::eval_expr(call_expr, ahfl::evaluator::EvalContext{}, call_eval);
    if (result.has_errors()) {
        print_diagnostics(result.diagnostics);
        return 1;
    }
    const auto *value = std::get_if<ahfl::evaluator::IntValue>(&result.value.node);
    if (value == nullptr || value->value != 19) {
        std::cerr << "unexpected trait dispatch runtime result: expected 19";
        if (value != nullptr) {
            std::cerr << ", got " << value->value;
        }
        std::cerr << '\n';
        return 1;
    }

    return 0;
}

int run_primitive_shadowing_forbidden(const std::filesystem::path &workspace_root) {
    const auto app_root = workspace_root / "app";
    const auto entry = app_root / "main.ahfl";

    std::error_code error;
    std::filesystem::remove_all(workspace_root, error);
    if (!write_text_file(entry,
                         "module app::main;\n"
                         "struct String {}\n")) {
        std::cerr << "failed to write primitive shadowing fixture\n";
        return 1;
    }

    ahfl::ProjectInput input;
    input.entry_files.push_back(entry);
    input.inject_prelude = false;
    input.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
        .prefix = "app",
        .root = app_root,
        .exported_modules = {"main"},
    });

    const ahfl::Frontend frontend;
    const auto parse_result = ahfl::parse_project(frontend, input);
    if (parse_result.has_errors()) {
        print_diagnostics(parse_result.diagnostics);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (!resolve_result.has_errors() ||
        !contains_code(resolve_result.diagnostics, "E::primitive_shadowing_forbidden")) {
        print_diagnostics(resolve_result.diagnostics);
        std::cerr << "expected primitive shadowing diagnostic\n";
        return 1;
    }

    return 0;
}

int run_primitive_facade_method_visibility(const std::filesystem::path &workspace_root,
                                           const std::filesystem::path &repo_root) {
    const auto app_root = workspace_root / "app";
    const auto without_import = app_root / "without_import.ahfl";
    const auto with_import = app_root / "with_import.ahfl";

    std::error_code error;
    std::filesystem::remove_all(workspace_root, error);
    if (!write_text_file(without_import,
                         "module app::without_import;\n"
                         "\n"
                         "fn length_of(s: String) -> Int effect Pure decreases 0 {\n"
                         "    return s.length();\n"
                         "}\n") ||
        !write_text_file(with_import,
                         "module app::with_import;\n"
                         "\n"
                         "import std::string;\n"
                         "\n"
                         "fn length_of(s: String) -> Int effect Pure decreases 0 {\n"
                         "    return s.length();\n"
                         "}\n")) {
        std::cerr << "failed to write primitive facade method fixture\n";
        return 1;
    }

    const auto make_input = [&](const std::filesystem::path &entry) {
        ahfl::ProjectInput input;
        input.entry_files.push_back(entry);
        input.inject_prelude = false;
        input.enforce_package_dependencies = true;
        input.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
            .prefix = "app",
            .root = app_root,
            .exported_modules = {"without_import", "with_import"},
            .dependency_prefixes = {"std"},
        });
        ahfl::test_support::append_repo_std_module_root(input, repo_root);
        return input;
    };

    const auto check_entry = [](const ahfl::ProjectInput &input,
                                ahfl::TypeCheckResult &type_check_result,
                                ahfl::DiagnosticBag &early_diagnostics) {
        const ahfl::Frontend frontend;
        const auto parse_result = ahfl::parse_project(frontend, input);
        if (parse_result.has_errors()) {
            early_diagnostics = parse_result.diagnostics;
            return false;
        }

        const ahfl::Resolver resolver;
        const auto resolve_result = resolver.resolve(parse_result.graph);
        if (resolve_result.has_errors()) {
            early_diagnostics = resolve_result.diagnostics;
            return false;
        }

        const ahfl::TypeChecker type_checker;
        type_check_result = type_checker.check(parse_result.graph, resolve_result);
        return true;
    };

    ahfl::TypeCheckResult without_import_result;
    ahfl::DiagnosticBag early_diagnostics;
    if (!check_entry(make_input(without_import), without_import_result, early_diagnostics)) {
        print_diagnostics(early_diagnostics);
        return 1;
    }
    if (!without_import_result.has_errors() ||
        !contains_code(without_import_result.diagnostics, "typecheck.UNKNOWN_CALLABLE") ||
        !contains_text(render_diagnostics(without_import_result.diagnostics), "String.length")) {
        print_diagnostics(without_import_result.diagnostics);
        std::cerr << "expected unimported std::string facade method to be hidden\n";
        return 1;
    }

    ahfl::TypeCheckResult with_import_result;
    early_diagnostics = ahfl::DiagnosticBag{};
    if (!check_entry(make_input(with_import), with_import_result, early_diagnostics)) {
        print_diagnostics(early_diagnostics);
        return 1;
    }
    if (with_import_result.has_errors()) {
        print_diagnostics(with_import_result.diagnostics);
        return 1;
    }

    return 0;
}

int run_inherent_method_visibility(const std::filesystem::path &workspace_root) {
    const auto app_root = workspace_root / "app";
    const auto lib_root = workspace_root / "lib";
    const auto app_entry = app_root / "main.ahfl";
    const auto lib_entry = lib_root / "types.ahfl";

    std::error_code error;
    std::filesystem::remove_all(workspace_root, error);
    if (!write_text_file(lib_entry,
                         "module lib::types;\n"
                         "\n"
                         "pub struct Widget {}\n"
                         "\n"
                         "impl Widget {\n"
                         "    pub fn visible(self) -> Int effect Pure decreases 0 {\n"
                         "        return 1;\n"
                         "    }\n"
                         "\n"
                         "    fn hidden(self) -> Int effect Pure decreases 0 {\n"
                         "        return 2;\n"
                         "    }\n"
                         "}\n") ||
        !write_text_file(app_entry,
                         "module app::main;\n"
                         "\n"
                         "import lib::types as types;\n"
                         "\n"
                         "fn check(w: types::Widget) -> Int effect Pure decreases 0 {\n"
                         "    let ok: Int = w.visible();\n"
                         "    return ok + w.hidden();\n"
                         "}\n")) {
        std::cerr << "failed to write inherent method visibility fixture\n";
        return 1;
    }

    ahfl::ProjectInput input;
    input.entry_files.push_back(app_entry);
    input.inject_prelude = false;
    input.enforce_package_dependencies = true;
    input.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
        .prefix = "app",
        .root = app_root,
        .exported_modules = {"main"},
        .dependency_prefixes = {"lib"},
    });
    input.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
        .prefix = "lib",
        .root = lib_root,
        .exported_modules = {"types"},
    });

    const ahfl::Frontend frontend;
    const auto parse_result = ahfl::parse_project(frontend, input);
    if (parse_result.has_errors()) {
        print_diagnostics(parse_result.diagnostics);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        print_diagnostics(resolve_result.diagnostics);
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    const auto type_check_result = type_checker.check(parse_result.graph, resolve_result);
    const auto rendered = render_diagnostics(type_check_result.diagnostics);
    if (!type_check_result.has_errors() ||
        !contains_code(type_check_result.diagnostics, "typecheck.UNKNOWN_CALLABLE") ||
        !contains_text(rendered, "lib::types::Widget.hidden") ||
        contains_text(rendered, "lib::types::Widget.visible")) {
        print_diagnostics(type_check_result.diagnostics);
        std::cerr << "expected cross-package hidden inherent method to be rejected\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 4) {
        std::cerr << "usage: project_check <case> <entry> <root>\n";
        return 2;
    }

    const std::string test_case = argv[1];
    const std::filesystem::path entry = argv[2];
    const std::filesystem::path root = argv[3];

    if (test_case == "ok-cross-file") {
        return run_ok_cross_file(entry, root);
    }

    if (test_case == "fail-node-input") {
        return run_fail_node_input(entry, root);
    }

    if (test_case == "fail-completed-state") {
        return run_fail_completed_state(entry, root);
    }

    if (test_case == "ok-expression-type-isolated") {
        return run_ok_expression_type_isolated(entry, root);
    }

    if (test_case == "ok-stdlib-runtime-api") {
        return run_ok_stdlib_runtime_api(entry, root);
    }

    if (test_case == "ok-trait-runtime-dispatch") {
        return run_ok_trait_runtime_dispatch(entry, root);
    }

    if (test_case == "primitive-shadowing-forbidden") {
        return run_primitive_shadowing_forbidden(entry);
    }

    if (test_case == "primitive-facade-method-visibility") {
        return run_primitive_facade_method_visibility(entry, root);
    }

    if (test_case == "inherent-method-visibility") {
        return run_inherent_method_visibility(entry);
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
