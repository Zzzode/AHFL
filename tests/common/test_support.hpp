#pragma once

#include "ahfl/frontend/frontend.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::test_support {

[[nodiscard]] inline bool diagnostics_contain(const DiagnosticBag &diagnostics,
                                              std::string_view needle) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(needle) != std::string::npos) {
            return true;
        }
    }

    return false;
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

[[nodiscard]] inline std::optional<ir::Program>
load_project_ir(const std::filesystem::path &project_descriptor) {
    const Frontend frontend;

    const auto descriptor_result = frontend.load_project_descriptor(project_descriptor);
    if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
        descriptor_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const auto project_result = frontend.parse_project(ProjectInput{
        .entry_files = descriptor_result.descriptor->entry_files,
        .search_roots = descriptor_result.descriptor->search_roots,
    });
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

} // namespace ahfl::test_support
