#include "ahfl/frontend/frontend.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

namespace {

void print_diagnostics(const ahfl::DiagnosticBag &diagnostics) {
    diagnostics.render(std::cout);
}

[[nodiscard]] std::string render_diagnostics(const ahfl::DiagnosticBag &diagnostics) {
    std::ostringstream out;
    diagnostics.render(out);
    return out.str();
}

[[nodiscard]] bool contains_text(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

[[nodiscard]] std::optional<std::string>
find_first_let_type(const ahfl::ir::Program &program, std::string_view flow_target) {
    for (const auto &declaration : program.declarations) {
        const auto *flow = std::get_if<ahfl::ir::FlowDecl>(&declaration);
        if (flow == nullptr || flow->target != flow_target) {
            continue;
        }

        for (const auto &handler : flow->state_handlers) {
            for (const auto &statement : handler.body.statements) {
                const auto *let_statement = std::get_if<ahfl::ir::LetStatement>(&statement->node);
                if (let_statement != nullptr) {
                    return let_statement->type;
                }
            }
        }
    }

    return std::nullopt;
}

int run_ok_cross_file(const std::filesystem::path &entry, const std::filesystem::path &root) {
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
    });
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
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
    });
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
    if (!contains_text(rendered, "type mismatch in workflow node input") ||
        !contains_text(rendered, "check_fail_input/app/main.ahfl")) {
        print_diagnostics(type_check_result.diagnostics);
        return 1;
    }

    return 0;
}

int run_fail_completed_state(const std::filesystem::path &entry, const std::filesystem::path &root) {
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
    });
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
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {entry},
        .search_roots = {root},
    });
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

    const auto ir_program = ahfl::lower_program_ir(parse_result.graph, resolve_result, type_check_result);
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

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
