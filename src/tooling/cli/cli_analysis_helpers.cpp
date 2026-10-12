#include "tooling/cli/cli_analysis_helpers.hpp"

#include "ahfl/compiler/backends/driver.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/assurance/assurance.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "verification/formal/checker.hpp"
#include "verification/formal/smt_bmc.hpp"
#include "verification/formal/smt_emit.hpp"
#include "verification/formal/subset.hpp"

#include "ahfl/base/support/diagnostics.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace ahfl::cli {

namespace {

[[nodiscard]] std::optional<int> parse_positive_seconds(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    int seconds = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        constexpr int kMaxAllowedSeconds = 24 * 60 * 60;
        seconds = (seconds * 10) + (character - '0');
        if (seconds > kMaxAllowedSeconds) {
            return std::nullopt;
        }
    }

    if (seconds <= 0) {
        return std::nullopt;
    }
    return seconds;
}

[[nodiscard]] std::optional<std::size_t> parse_bmc_depth(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    constexpr std::size_t kMaxBmcDepth = 1'000'000;
    std::size_t depth = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (depth > (kMaxBmcDepth - digit) / 10) {
            return std::nullopt;
        }
        depth = depth * 10 + digit;
    }
    if (depth == 0) {
        return std::nullopt;
    }
    return depth;
}

[[nodiscard]] std::optional<bool> parse_bool_flag(std::string_view value) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// compile_to_ir — shared compile pipeline
// ---------------------------------------------------------------------------

std::optional<ir::Program> compile_to_ir(const std::filesystem::path &file_path,
                                         std::ostream &diagnostics) {
    const Frontend frontend;
    const auto parse_result = ahfl::parse_project(frontend,
                                                  ProjectInput{
                                                      .entry_files = {file_path},
                                                      .inject_prelude = false,
                                                  });
    if (parse_result.has_errors()) {
        parse_result.diagnostics.render(diagnostics);
        return std::nullopt;
    }

    const Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        resolve_result.diagnostics.render(diagnostics);
        return std::nullopt;
    }

    const TypeChecker type_checker;
    const auto type_check_result = type_checker.check(parse_result.graph, resolve_result);
    if (type_check_result.has_errors()) {
        type_check_result.diagnostics.render(diagnostics);
        return std::nullopt;
    }

    const Validator validator;
    const auto validation_result =
        validator.validate(parse_result.graph, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        validation_result.diagnostics.render(diagnostics);
        return std::nullopt;
    }

    return lower_program_ir(parse_result.graph, resolve_result, type_check_result);
}

namespace {

struct SmvSizeReport {
    std::size_t bytes{0};
    std::size_t lines{0};
    std::size_t ltlspecs{0};
};

[[nodiscard]] SmvSizeReport measure_smv_output(std::string_view output) {
    SmvSizeReport report{.bytes = output.size()};
    std::string_view remaining = output;
    while (!remaining.empty()) {
        const auto newline = remaining.find('\n');
        if (newline == std::string_view::npos) {
            break;
        }
        ++report.lines;
        remaining.remove_prefix(newline + 1);
    }
    if (!output.empty() && output.back() != '\n') {
        ++report.lines;
    }

    std::size_t offset = 0;
    while (offset < output.size()) {
        const auto found = output.find("LTLSPEC", offset);
        if (found == std::string_view::npos) {
            break;
        }
        if (found == 0 || output[found - 1] == '\n') {
            ++report.ltlspecs;
        }
        offset = found + std::string_view{"LTLSPEC"}.size();
    }
    return report;
}

void print_smv_size_report(const SmvSizeReport &report, std::ostream &err) {
    err << "smv size report:\n"
        << "  bytes: " << report.bytes << '\n'
        << "  lines: " << report.lines << '\n'
        << "  ltlspecs: " << report.ltlspecs << '\n';
}

} // namespace

// ---------------------------------------------------------------------------
// Build output path resolution
// ---------------------------------------------------------------------------

namespace {

// Resolve the WASM output path for `ahflc build`. Priority:
//   1. --output <path>
//   2. <stem>.wasm derived from the input file (single-file mode)
//   3. package name + ".wasm" (package mode)
// Returns empty string if no path can be derived (caller errors out).
[[nodiscard]] std::string
resolve_build_output_path(const CommandLineOptions &options,
                          const ahfl::handoff::PackageMetadata *package_metadata) {
    if (options.build_output_path.has_value() && !options.build_output_path->empty()) {
        return std::string{*options.build_output_path};
    }

    // Single-file mode: derive <stem>.wasm from the input file path.
    if (!options.positional.empty()) {
        std::filesystem::path input{std::string{options.positional[0]}};
        if (input.has_extension() && input.extension() == ".ahfl") {
            return input.replace_extension(".wasm").string();
        }
        // Non-.ahfl positional (e.g. a directory in package mode): fall through.
    }

    // Package mode: use the package name.
    if (package_metadata != nullptr && package_metadata->identity.has_value() &&
        !package_metadata->identity->name.empty()) {
        return package_metadata->identity->name + ".wasm";
    }

    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// emit_core_backend (non-template overload)
// ---------------------------------------------------------------------------

std::optional<int> emit_core_backend(std::optional<CommandKind> effective_command,
                                     ahfl::ir::Program &program,
                                     const ahfl::ResolveResult &resolve_result,
                                     const ahfl::TypeCheckResult &type_check_result,
                                     const ahfl::handoff::PackageMetadata *package_metadata,
                                     const CommandLineOptions &options,
                                     std::ostream &out,
                                     std::ostream &err) {
    static_cast<void>(resolve_result);
    static_cast<void>(type_check_result);

    const auto backend = core_backend_for_command(effective_command);
    if (!backend.has_value()) {
        return std::nullopt;
    }

    // RFC 0019: resolve the WASM deployment profile (build only).
    auto wasm_profile = ahfl::WasmProfile::Wasi;
    if (options.wasm_profile.has_value()) {
        if (*options.wasm_profile == "browser") {
            wasm_profile = ahfl::WasmProfile::Browser;
        } else if (*options.wasm_profile == "wasi") {
            wasm_profile = ahfl::WasmProfile::Wasi;
        } else {
            err << "error: unknown --wasm-profile '" << *options.wasm_profile
                << "' (expected wasi or browser)\n";
            return 1;
        }
    }

    // Some backends lower `ir::Program` into a different layer and surface a
    // structured `code: message` diagnostic when the source is outside what that
    // lowering supports yet (WASM profile/layout rejections, Core-IR
    // `core.UNLOWERED_*`). Those are user-actionable conditions, not internal
    // faults, so they are reported without the "internal error" prefix.
    const bool reports_user_facing_lowering_diagnostic =
        *backend == ahfl::BackendKind::InfraWasm || *backend == ahfl::BackendKind::CoreIrJson ||
        *backend == ahfl::BackendKind::InfraK8sCrd;

    const bool needs_smv_size_report =
        options.smv_size_report_requested && effective_command == CommandKind::EmitSmv;

    // `ahflc build` writes the WASM binary to a file (default <stem>.wasm),
    // not stdout. All other backend commands write to stdout. Buffer the
    // output so a failed backend leaves no artifact behind.
    const bool is_build = effective_command == CommandKind::Build;
    std::string build_path;
    if (is_build) {
        build_path = resolve_build_output_path(options, package_metadata);
        if (build_path.empty()) {
            err << "error: cannot determine build output path; pass --output <path>\n";
            return 1;
        }
    }

    // --emit-descriptor is only meaningful for the WASM backend: the embedded
    // host execution descriptor is a product of WASM emission. Reject it on
    // any other backend rather than silently producing no file.
    if (options.emit_descriptor_path.has_value() && *backend != ahfl::BackendKind::InfraWasm) {
        err << "error: --emit-descriptor is only valid with the wasm backend (ahflc build)\n";
        return 1;
    }
    std::string descriptor_json;
    std::string *const descriptor_out =
        options.emit_descriptor_path.has_value() ? &descriptor_json : nullptr;

    std::ostringstream build_buffer;
    std::ostream &backend_out = is_build ? build_buffer : out;

    if (!needs_smv_size_report) {
        auto result = ahfl::emit_backend(*backend, program, backend_out, package_metadata,
                                         wasm_profile, descriptor_out);
        if (!result.has_value()) {
            if (reports_user_facing_lowering_diagnostic) {
                err << "error: " << result.error() << "\n";
            } else {
                err << "internal error: core backend command ";
                if (effective_command.has_value()) {
                    err << "'" << command_name(*effective_command) << "' ";
                }
                err << "failed: " << result.error() << "\n";
            }
            return 1;
        }
        if (is_build) {
            std::ofstream build_file(build_path, std::ios::binary | std::ios::trunc);
            if (!build_file.is_open()) {
                err << "error: cannot open output file '" << build_path << "' for writing\n";
                return 1;
            }
            build_file << build_buffer.str();
            build_file.close();
            out << "ok: built " << build_path << '\n';
            if (descriptor_out != nullptr && !descriptor_json.empty()) {
                std::ofstream descriptor_file(std::string{*options.emit_descriptor_path},
                                              std::ios::binary | std::ios::trunc);
                if (!descriptor_file.is_open()) {
                    err << "error: cannot open descriptor file '"
                        << *options.emit_descriptor_path << "' for writing\n";
                    return 1;
                }
                descriptor_file << descriptor_json;
                descriptor_file.close();
                out << "ok: emitted descriptor " << *options.emit_descriptor_path << '\n';
            }
        }
        return 0;
    }

    std::ostringstream buffered_output;
    auto result =
        ahfl::emit_backend(*backend, program, buffered_output, package_metadata, wasm_profile);
    if (!result.has_value()) {
        err << "internal error: core backend command ";
        if (effective_command.has_value()) {
            err << "'" << command_name(*effective_command) << "' ";
        }
        err << "failed: " << result.error() << "\n";
        return 1;
    }

    const auto output = buffered_output.str();
    out << output;
    print_smv_size_report(measure_smv_output(output), err);

    return 0;
}

// ---------------------------------------------------------------------------
// validate_assurance_program
// ---------------------------------------------------------------------------

int validate_assurance_program(const ahfl::ir::Program &program) {
    const auto bundle = ahfl::assurance::build_assurance_bundle(program);
    const auto validation = ahfl::assurance::validate_assurance_bundle(bundle);
    ahfl::assurance::print_assurance_validation_report(validation,
                                                       validation.ok ? std::cout : std::cerr);
    return validation.ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// run_pipeline_formal_verification (RFC 0028)
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] ahfl::formal::FormalCheckerOptions
build_formal_checker_options(const CommandLineOptions &options) {
    ahfl::formal::FormalCheckerOptions formal_options;
    if (options.formal_backend.has_value()) {
        if (auto backend = ahfl::formal::parse_model_checker_kind(*options.formal_backend);
            backend.has_value()) {
            formal_options.backend_kind = *backend;
        }
    }
    if (options.model_checker.has_value()) {
        formal_options.checker_path = std::string(*options.model_checker);
    }
    if (options.formal_model_out.has_value()) {
        formal_options.model_output_path = std::filesystem::path(*options.formal_model_out);
    }
    if (options.checker_timeout_seconds.has_value()) {
        if (const auto seconds = parse_positive_seconds(*options.checker_timeout_seconds);
            seconds.has_value()) {
            formal_options.checker_timeout = std::chrono::seconds{*seconds};
        }
    }
    formal_options.explain = options.explain_requested;
    formal_options.verify_data_contracts = options.verify_data_contracts;

    if (options.bmc_depth.has_value()) {
        if (auto depth = parse_bmc_depth(*options.bmc_depth); depth.has_value()) {
            formal_options.bmc_depth = *depth;
            formal_options.bmc_use_bmc_engine = true;
        }
    }
    if (options.bmc_boundary_invariants.has_value()) {
        if (auto flag = parse_bool_flag(*options.bmc_boundary_invariants); flag.has_value()) {
            formal_options.bmc_boundary_invariants = *flag;
        }
    }

    return formal_options;
}

} // namespace

bool run_pipeline_formal_verification(const ahfl::ir::Program &program,
                                      const CommandLineOptions &options,
                                      std::ostream &out,
                                      std::ostream &err) {
    // Verification is a diagnostic stage; keep stdout available for command
    // output such as run --output-format json.
    (void)out;
    ir::ContractClauseIndex clause_index;
    clause_index.rebuild(program);
    std::size_t assumptions = 0;
    std::size_t goals = 0;
    for (const auto &decl : program.declarations) {
        const auto *contract = std::get_if<ir::ContractDecl>(&decl);
        if (contract == nullptr)
            continue;
        for (const auto &clause : contract->clauses) {
            if (!std::holds_alternative<ir::ExprRef>(clause.value) ||
                clause.kind == ir::ContractClauseKind::Decreases)
                continue;
            if (clause.kind == ir::ContractClauseKind::Requires)
                ++assumptions;
            else
                ++goals;
        }
    }
    err << "data_contract_scope: contract_implication\n"
        << "data_contract_assumptions: " << assumptions << '\n'
        << "data_contract_goals: " << goals << '\n'
        << "runtime_contract_checks: not_implemented\n";
    for (const auto &decl : program.declarations) {
        const auto *contract = std::get_if<ir::ContractDecl>(&decl);
        if (contract == nullptr)
            continue;
        for (std::size_t i = 0; i < contract->clauses.size(); ++i) {
            const auto &clause = contract->clauses[i];
            if (!std::holds_alternative<ir::ExprRef>(clause.value) ||
                clause.kind == ir::ContractClauseKind::Decreases)
                continue;
            err << (clause.kind == ir::ContractClauseKind::Requires ? "data_contract_assumption: "
                                                                    : "data_contract_goal: ")
                << ir::symbol_canonical_name(contract->target_ref) << " "
                << ir::contract_clause_kind_name(clause.kind) << "["
                << clause_index.clause_begin(*contract) + i << "] "
                << (options.verify_data_contracts ? "requested" : "not_requested") << " "
                << contract->provenance.source_path;
            if (clause.source_range) {
                err << " @[" << clause.source_range->begin_offset << ","
                    << clause.source_range->end_offset << ")";
            }
            err << '\n';
        }
    }

    const auto smv_result = ahfl::formal::verify_program_with_smv_checker(
        program, build_formal_checker_options(options));
    ahfl::formal::print_formal_verification_report(smv_result, err);
    const bool control_passed = ahfl::formal::is_formal_verification_success(smv_result);
    if (!options.verify_data_contracts) {
        err << "data_contract_status: not_requested\n";
        return control_passed;
    }

    const auto smt_result = ahfl::formal::run_smt_bmc(program, {});
    std::string_view data_status = ahfl::formal::smt_bmc_status_name(smt_result.status);
    switch (smt_result.status) {
    case ahfl::formal::SmtBmcStatus::Safe:
        data_status = "proved";
        break;
    case ahfl::formal::SmtBmcStatus::BoundedSafe:
        data_status = "bounded";
        break;
    case ahfl::formal::SmtBmcStatus::Unsafe:
        data_status = "refuted";
        break;
    case ahfl::formal::SmtBmcStatus::Unsupported:
        if (smt_result.unsupported_clauses.empty())
            data_status = "no_targets";
        break;
    default:
        break;
    }
    err << "data_contract_status: " << data_status << '\n';
    ahfl::formal::print_smt_bmc_report(smt_result, err);
    for (const auto &clause : smt_result.unsupported_clauses) {
        err << "error[" << ahfl::error_codes::formal::NotInVerifiedSubset.full_code()
            << "]: requested contract " << clause.target_name << " "
            << ir::contract_clause_kind_name(clause.kind) << "[" << clause.clause_index
            << "] cannot be proven: " << ahfl::formal::describe_rejection(clause.rejection);
        if (const auto location =
                clause_index.find_clause(clause.target_name, clause.clause_index)) {
            err << " " << location->contract->provenance.source_path;
        }
        if (clause.source_range) {
            err << " @[" << clause.source_range->begin_offset << ","
                << clause.source_range->end_offset << ")";
        }
        err << '\n';
    }
    return control_passed && !ahfl::formal::smt_bmc_result_is_failure(smt_result);
}

// ---------------------------------------------------------------------------
// emit_smt_artifact
// ---------------------------------------------------------------------------

int emit_smt_artifact(const ahfl::ir::Program &program, std::ostream &out) {
    ahfl::formal::emit_program_smt(program, out);
    return 0;
}

// ---------------------------------------------------------------------------
// read_text_file
// ---------------------------------------------------------------------------

bool read_text_file(const std::filesystem::path &path,
                    std::string &content,
                    std::ostream &diagnostics) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics << "error: failed to open capability mock descriptor: "
                    << ahfl::display_path(path) << '\n';
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

// ---------------------------------------------------------------------------
// dump_ast_outline
// ---------------------------------------------------------------------------

void dump_ast_outline(const ahfl::ast::Program &program, std::ostream &out) {
    ahfl::dump_program_outline(program, out);
}

void dump_ast_outline(const ahfl::SourceGraph &graph, std::ostream &out) {
    ahfl::dump_source_graph_ast_outline(graph, out);
}

// ---------------------------------------------------------------------------
// print_success_summary
// ---------------------------------------------------------------------------

void print_success_summary(const ahfl::ast::Program &program,
                           const ahfl::ResolveResult &resolve_result,
                           const ahfl::TypeCheckResult &type_check_result,
                           std::ostream &out) {
    out << "ok: checked " << program.declarations.size() << " top-level declaration(s), "
        << resolve_result.symbol_table.symbols().size() << " symbol(s), "
        << resolve_result.references().size() << " reference(s), "
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
        << resolve_result.references().size() << " reference(s), "
        << type_check_result.environment.structs().size() +
               type_check_result.environment.enums().size()
        << " named type(s)\n";
}

} // namespace ahfl::cli
