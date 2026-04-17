#include "ahfl/backend.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using MaybeSourceFile = std::optional<std::reference_wrapper<const ahfl::SourceFile>>;

struct CommandLineOptions {
    bool dump_ast{false};
    bool dump_types{false};
    bool dump_project{false};
    bool emit_ir{false};
    bool emit_ir_json{false};
    bool emit_native_json{false};
    bool emit_summary{false};
    bool emit_smv{false};
    std::optional<std::string_view> project_descriptor;
    std::optional<std::string_view> workspace_descriptor;
    std::optional<std::string_view> project_name;
    std::vector<std::string_view> search_roots;
    std::vector<std::string_view> positional;
};

void print_usage(std::ostream &out) {
    out << "Usage:\n"
        << "  ahflc <check|dump-ast|dump-project|dump-types|emit-ir|emit-ir-json|emit-native-json|emit-summary|emit-smv> --project <ahfl.project.json>\n"
        << "  ahflc <check|dump-ast|dump-project|dump-types|emit-ir|emit-ir-json|emit-native-json|emit-summary|emit-smv> --workspace <ahfl.workspace.json> --project-name <name>\n"
        << "  ahflc check [--search-root <dir>]... [--dump-ast] <input.ahfl>\n"
        << "  ahflc dump-ast [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc dump-types [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc dump-project [--search-root <dir>]... <entry.ahfl>\n"
        << "  ahflc emit-ir [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-ir-json [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-native-json [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-summary [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-smv [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc [--dump-ast] <input.ahfl>\n";
}

[[nodiscard]] bool is_subcommand(std::string_view argument) {
    return argument == "check" || argument == "dump-ast" || argument == "dump-types" ||
           argument == "dump-project" || argument == "emit-ir" || argument == "emit-ir-json" ||
           argument == "emit-native-json" || argument == "emit-summary" ||
           argument == "emit-smv";
}

[[nodiscard]] std::optional<ahfl::BackendKind>
selected_backend(const CommandLineOptions &options) {
    if (options.emit_ir) {
        return ahfl::BackendKind::Ir;
    }

    if (options.emit_ir_json) {
        return ahfl::BackendKind::IrJson;
    }

    if (options.emit_native_json) {
        return ahfl::BackendKind::NativeJson;
    }

    if (options.emit_summary) {
        return ahfl::BackendKind::Summary;
    }

    if (options.emit_smv) {
        return ahfl::BackendKind::Smv;
    }

    return std::nullopt;
}

template <typename InputT>
[[nodiscard]] bool emit_selected_backend(const CommandLineOptions &options,
                                         const InputT &input,
                                         const ahfl::ResolveResult &resolve_result,
                                         const ahfl::TypeCheckResult &type_check_result,
                                         std::ostream &out) {
    const auto backend = selected_backend(options);
    if (!backend.has_value()) {
        return false;
    }

    ahfl::emit_backend(*backend, input, resolve_result, type_check_result, out);
    return true;
}

template <typename ResultT>
void render_diagnostics(const ResultT &result, MaybeSourceFile source_file, std::ostream &out) {
    if (source_file.has_value()) {
        result.diagnostics.render(out, *source_file);
        return;
    }

    result.diagnostics.render(out);
}

void dump_ast_outline(const ahfl::ast::Program &program, std::ostream &out) {
    ahfl::dump_program_outline(program, out);
}

void dump_ast_outline(const ahfl::SourceGraph &graph, std::ostream &out) {
    ahfl::dump_project_ast_outline(graph, out);
}

void print_success_summary(const ahfl::ast::Program &program,
                           const ahfl::ResolveResult &resolve_result,
                           const ahfl::TypeCheckResult &type_check_result,
                           std::ostream &out) {
    out << "ok: checked " << program.declarations.size() << " top-level declaration(s), "
        << resolve_result.symbol_table.symbols().size() << " symbol(s), "
        << resolve_result.references.size() << " reference(s), "
        << type_check_result.environment.structs().size() +
               type_check_result.environment.enums().size()
        << " named type(s)\n";
}

void print_success_summary(const ahfl::SourceGraph &graph,
                           const ahfl::ResolveResult &resolve_result,
                           const ahfl::TypeCheckResult &type_check_result,
                           std::ostream &out) {
    out << "ok: checked " << graph.sources.size() << " source(s), "
        << resolve_result.symbol_table.symbols().size() << " symbol(s), "
        << resolve_result.references.size() << " reference(s), "
        << type_check_result.environment.structs().size() +
               type_check_result.environment.enums().size()
        << " named type(s)\n";
}

[[nodiscard]] int load_project_input(const CommandLineOptions &options,
                                     const ahfl::Frontend &frontend,
                                     ahfl::ProjectInput &input) {
    if (options.project_descriptor.has_value()) {
        auto descriptor_result =
            frontend.load_project_descriptor(std::string(*options.project_descriptor));
        descriptor_result.diagnostics.render(std::cerr);
        if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
            return descriptor_result.has_errors() ? 1 : 0;
        }

        input.entry_files = descriptor_result.descriptor->entry_files;
        input.search_roots = descriptor_result.descriptor->search_roots;
        return -1;
    }

    if (options.workspace_descriptor.has_value()) {
        auto descriptor_result = frontend.load_project_descriptor_from_workspace(
            std::string(*options.workspace_descriptor), *options.project_name);
        descriptor_result.diagnostics.render(std::cerr);
        if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
            return descriptor_result.has_errors() ? 1 : 0;
        }

        input.entry_files = descriptor_result.descriptor->entry_files;
        input.search_roots = descriptor_result.descriptor->search_roots;
        return -1;
    }

    input.entry_files.push_back(std::string(options.positional.front()));
    input.search_roots.reserve(options.search_roots.size());
    for (const auto search_root : options.search_roots) {
        input.search_roots.push_back(std::string(search_root));
    }

    return -1;
}

template <typename InputT>
[[nodiscard]] int run_analysis_pipeline(const CommandLineOptions &options,
                                        const InputT &input,
                                        MaybeSourceFile source_file) {
    const ahfl::Resolver resolver;
    auto resolve_result = resolver.resolve(input);
    render_diagnostics(resolve_result, source_file, std::cerr);
    if (resolve_result.has_errors()) {
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    auto type_check_result = type_checker.check(input, resolve_result);
    render_diagnostics(type_check_result, source_file, std::cerr);

    if (options.dump_types) {
        ahfl::dump_type_environment(type_check_result.environment,
                                    resolve_result.symbol_table,
                                    std::cout);
    }

    if (type_check_result.has_errors()) {
        return 1;
    }

    const ahfl::Validator validator;
    auto validation_result = validator.validate(input, resolve_result, type_check_result);
    render_diagnostics(validation_result, source_file, std::cerr);
    if (validation_result.has_errors()) {
        return 1;
    }

    if (emit_selected_backend(options, input, resolve_result, type_check_result, std::cout)) {
        return 0;
    }

    if (!options.dump_types) {
        print_success_summary(input, resolve_result, type_check_result, std::cout);
    }

    return 0;
}

[[nodiscard]] int parse_command_line(std::span<const std::string_view> arguments,
                                     CommandLineOptions &options) {
    bool explicit_subcommand = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            return 0;
        }

        if (argument == "--project") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --project requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.project_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--workspace") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --workspace requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.workspace_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--project-name") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --project-name requires a project name\n";
                print_usage(std::cerr);
                return 2;
            }

            options.project_name = arguments[++index];
            continue;
        }

        if (argument == "--search-root") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --search-root requires a directory path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.search_roots.push_back(arguments[++index]);
            continue;
        }

        if (argument == "--dump-ast") {
            options.dump_ast = true;
            continue;
        }

        if (argument == "--dump-types") {
            options.dump_types = true;
            continue;
        }

        if (is_subcommand(argument) && !explicit_subcommand && options.positional.empty()) {
            explicit_subcommand = true;
            if (argument == "dump-ast") {
                options.dump_ast = true;
            }
            if (argument == "dump-types") {
                options.dump_types = true;
            }
            if (argument == "dump-project") {
                options.dump_project = true;
            }
            if (argument == "emit-ir") {
                options.emit_ir = true;
            }
            if (argument == "emit-ir-json") {
                options.emit_ir_json = true;
            }
            if (argument == "emit-native-json") {
                options.emit_native_json = true;
            }
            if (argument == "emit-summary") {
                options.emit_summary = true;
            }
            if (argument == "emit-smv") {
                options.emit_smv = true;
            }
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "error: unknown option '" << argument << "'\n";
            print_usage(std::cerr);
            return 2;
        }

        options.positional.push_back(argument);
    }

    return -1;
}

int run_cli(std::span<const std::string_view> arguments) {
    CommandLineOptions options;
    if (const auto parse_status = parse_command_line(arguments, options); parse_status >= 0) {
        return parse_status;
    }

    const auto action_count =
        static_cast<int>(options.dump_ast) + static_cast<int>(options.dump_types) +
        static_cast<int>(options.dump_project) + static_cast<int>(options.emit_ir) +
        static_cast<int>(options.emit_ir_json) + static_cast<int>(options.emit_native_json) +
        static_cast<int>(options.emit_summary) + static_cast<int>(options.emit_smv);
    if (action_count > 1) {
        std::cerr << "error: choose at most one of dump-ast, dump-types, dump-project, emit-ir, "
                     "emit-ir-json, emit-native-json, emit-summary, or emit-smv\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_descriptor.has_value() && !options.search_roots.empty()) {
        std::cerr << "error: --project cannot be combined with --search-root\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && options.project_descriptor.has_value()) {
        std::cerr << "error: --workspace cannot be combined with --project\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && !options.search_roots.empty()) {
        std::cerr << "error: --workspace cannot be combined with --search-root\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_name.has_value() && !options.workspace_descriptor.has_value()) {
        std::cerr << "error: --project-name requires --workspace\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && !options.project_name.has_value()) {
        std::cerr << "error: --workspace requires --project-name\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_descriptor.has_value() || options.workspace_descriptor.has_value()
            ? !options.positional.empty()
            : options.positional.size() != 1) {
        print_usage(std::cerr);
        return 2;
    }

    const bool project_mode = options.project_descriptor.has_value() ||
                              options.workspace_descriptor.has_value() ||
                              !options.search_roots.empty();
    const ahfl::Frontend frontend;
    if (options.dump_project || project_mode) {
        ahfl::ProjectInput input;
        if (const auto load_status = load_project_input(options, frontend, input);
            load_status >= 0) {
            return load_status;
        }

        auto project_result = frontend.parse_project(input);
        render_diagnostics(project_result, std::nullopt, std::cerr);
        if (project_result.has_errors()) {
            return 1;
        }

        if (options.dump_project) {
            ahfl::dump_project_outline(project_result.graph, std::cout);
            return 0;
        }

        if (options.dump_ast) {
            dump_ast_outline(project_result.graph, std::cout);
            return 0;
        }

        return run_analysis_pipeline(options, project_result.graph, std::nullopt);
    }

    auto parse_result = frontend.parse_file(std::string(options.positional.front()));
    render_diagnostics(parse_result, std::cref(parse_result.source), std::cerr);

    if (parse_result.program && options.dump_ast) {
        dump_ast_outline(*parse_result.program, std::cout);
    }

    if (parse_result.has_errors() || !parse_result.program) {
        return parse_result.has_errors() ? 1 : 0;
    }

    return run_analysis_pipeline(options, *parse_result.program, std::cref(parse_result.source));
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));

    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    return run_cli(arguments);
}
