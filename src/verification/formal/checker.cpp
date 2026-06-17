#include "verification/formal/checker.hpp"

#include "verification/formal/counterexample.hpp"
#include "verification/formal/counterexample_json.hpp"
#include "verification/formal/model_checker_backend.hpp"
#include "verification/formal/process_launcher.hpp"

#if defined(AHFL_ENABLE_BACKEND_FORMAL)
#include "ahfl/compiler/backends/smv.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ahfl::formal {

namespace {

void print_state_space_estimate(const StateSpaceEstimate &estimate, std::ostream &out) {
    out << "state_space_estimate: " << estimate.estimated_states << '\n';
    out << "state_space_agents: " << estimate.num_agents << '\n';
    out << "state_space_max_states_per_agent: " << estimate.max_states_per_agent << '\n';
    out << "state_space_transitions: " << estimate.total_transitions << '\n';
    out << "state_space_likely_tractable: " << (estimate.likely_tractable ? "true" : "false")
        << '\n';
    if (!estimate.estimation_method.empty()) {
        out << "state_space_method: " << estimate.estimation_method << '\n';
    }
    if (!estimate.warning.empty()) {
        out << "state_space_warning: " << estimate.warning << '\n';
    }
}

} // namespace

#if defined(AHFL_ENABLE_BACKEND_FORMAL)
namespace {

struct CheckerResolution {
    ModelCheckerKind backend_kind{ModelCheckerKind::NuXmv};
    ModelCheckerCapabilities capabilities;
    ModelCheckerAvailability availability;
};

[[nodiscard]] CheckerResolution resolve_checker(const FormalCheckerOptions &options) {
    auto backend = create_backend(options.backend_kind);
    CheckerResolution resolution{
        .backend_kind = options.backend_kind,
        .capabilities = backend->capabilities(),
        .availability = backend->availability(),
    };

    if (!resolution.capabilities.supports_external_verification ||
        !resolution.capabilities.supports_ahfl_smv_semantics) {
        resolution.availability = ModelCheckerAvailability{
            .status = ModelCheckerAvailabilityStatus::VerificationUnsupported,
            .binary_path = {},
            .reason = resolution.capabilities.skip_reason,
        };
        return resolution;
    }

    if (options.checker_path.has_value() && !options.checker_path->empty()) {
        resolution.availability = ModelCheckerAvailability{
            .status = ModelCheckerAvailabilityStatus::Available,
            .binary_path = *options.checker_path,
            .reason = {},
        };
        return resolution;
    }

    if (const auto *env_value = std::getenv("AHFL_SMV_CHECKER");
        env_value != nullptr && std::string_view(env_value).size() > 0) {
        resolution.availability = ModelCheckerAvailability{
            .status = ModelCheckerAvailabilityStatus::Available,
            .binary_path = std::string(env_value),
            .reason = {},
        };
        return resolution;
    }

    return resolution;
}

[[nodiscard]] std::filesystem::path make_temp_model_path() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("ahfl-formal-model-" + std::to_string(now) + ".smv");
}

struct ProcessResult {
    int exit_code{-1};
    std::string output;
    bool timed_out{false};
};

[[nodiscard]] ProcessResult run_checker(std::string_view checker_path,
                                        const std::filesystem::path &model_path,
                                        std::chrono::seconds timeout) {
    ProcessConfig config;
    config.executable = std::string(checker_path);
    config.arguments = {model_path.string()};
    config.timeout = timeout;
    const auto result = launch_process(config);
    auto output = result.stdout_output;
    if (!result.stderr_output.empty()) {
        output += result.stderr_output;
    }
    return ProcessResult{
        .exit_code = result.exit_code,
        .output = std::move(output),
        .timed_out = result.timed_out,
    };
}

[[nodiscard]] std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    for (const auto character : text) {
        if (character == '\n') {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }
            lines.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(character);
    }
    if (!current.empty()) {
        lines.push_back(std::move(current));
    }
    return lines;
}

[[nodiscard]] std::string lower_copy(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] bool contains(std::string_view value, std::string_view needle) {
    return value.find(needle) != std::string_view::npos;
}

[[nodiscard]] std::string trim_copy(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

[[nodiscard]] std::unordered_map<std::string, std::string>
parse_symbol_mappings(std::string_view model) {
    constexpr std::string_view prefix = "-- AHFL_MAP ";
    constexpr std::string_view separator = " => ";

    std::unordered_map<std::string, std::string> mappings;
    for (const auto &line : split_lines(model)) {
        if (!line.starts_with(prefix)) {
            continue;
        }

        const auto payload = std::string_view(line).substr(prefix.size());
        const auto separator_position = payload.find(separator);
        if (separator_position == std::string_view::npos) {
            continue;
        }

        mappings.emplace(trim_copy(payload.substr(0, separator_position)),
                         trim_copy(payload.substr(separator_position + separator.size())));
    }

    return mappings;
}

void append_counterexample_mapping(
    FormalVerificationResult &result,
    std::string_view line,
    const std::unordered_map<std::string, std::string> &symbol_mappings) {
    const auto trimmed = trim_copy(line);
    const auto separator = trimmed.find('=');
    if (separator == std::string::npos) {
        return;
    }

    const auto symbol = trim_copy(std::string_view(trimmed).substr(0, separator));
    const auto mapping = symbol_mappings.find(symbol);
    if (mapping == symbol_mappings.end()) {
        return;
    }

    const auto rendered = symbol + " => " + mapping->second;
    if (std::ranges::find(result.counterexample_mappings, rendered) ==
        result.counterexample_mappings.end()) {
        result.counterexample_mappings.push_back(rendered);
    }
}

void parse_checker_output(FormalVerificationResult &result,
                          const std::unordered_map<std::string, std::string> &symbol_mappings) {
    bool capture_counterexample = false;
    for (const auto &line : split_lines(result.output)) {
        const auto lowered = lower_copy(line);
        if (contains(lowered, " is false")) {
            result.failing_specifications.push_back(line);
        } else if (contains(lowered, " is true")) {
            result.proven_specifications.push_back(line);
        }

        if (contains(lowered, "counterexample") || contains(lowered, "trace description") ||
            contains(lowered, "trace type")) {
            capture_counterexample = true;
        }

        if (capture_counterexample && result.counterexample_excerpt.size() < 20) {
            result.counterexample_excerpt.push_back(line);
            append_counterexample_mapping(result, line, symbol_mappings);
        }
    }
}

[[nodiscard]] std::size_t count_expected_specifications(std::string_view model) {
    std::size_t count = 0;
    for (const auto &line : split_lines(model)) {
        if (contains(lower_copy(line), "ltlspec")) {
            ++count;
        }
    }
    return count;
}

void print_output_excerpt(std::string_view output, std::ostream &out) {
    std::size_t printed = 0;
    for (const auto &line : split_lines(output)) {
        if (printed >= 20) {
            out << "... output truncated ...\n";
            return;
        }
        out << line << '\n';
        ++printed;
    }
}

} // namespace

#endif

std::optional<ModelCheckerKind> parse_model_checker_kind(std::string_view value) {
    std::string normalized(value);
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (normalized == "nusmv") {
        return ModelCheckerKind::NuSMV;
    }
    if (normalized == "nuxmv") {
        return ModelCheckerKind::NuXmv;
    }
    if (normalized == "spin") {
        return ModelCheckerKind::SPIN;
    }
    if (normalized == "tlaplus" || normalized == "tla+" || normalized == "tla") {
        return ModelCheckerKind::TLAPlus;
    }
    return std::nullopt;
}

std::string_view model_checker_kind_name(ModelCheckerKind kind) {
    switch (kind) {
    case ModelCheckerKind::NuSMV:
        return "nusmv";
    case ModelCheckerKind::NuXmv:
        return "nuxmv";
    case ModelCheckerKind::SPIN:
        return "spin";
    case ModelCheckerKind::TLAPlus:
        return "tlaplus";
    }
    return "nuxmv";
}

bool is_formal_verification_success(const FormalVerificationResult &result) {
    return result.status == FormalVerificationStatus::Passed;
}

FormalVerificationResult verify_program_with_smv_checker(const ir::Program &program,
                                                         const FormalCheckerOptions &options) {
    FormalVerificationResult result;
    result.state_space_estimate = estimate_state_space(program);
#if !defined(AHFL_ENABLE_BACKEND_FORMAL)
    (void)program;
    (void)options;
    result.status = FormalVerificationStatus::CheckerError;
    result.error_message = "SMV backend support is disabled in this build";
    return result;
#else
    const auto checker = resolve_checker(options);
    result.backend_kind = checker.backend_kind;
    result.capabilities = checker.capabilities;
    result.availability_status = checker.availability.status;
    result.checker_timeout = options.checker_timeout;

    if (checker.availability.status == ModelCheckerAvailabilityStatus::VerificationUnsupported) {
        result.status = FormalVerificationStatus::VerificationUnsupported;
        result.error_message = checker.availability.reason;
        return result;
    }

    if (checker.availability.status == ModelCheckerAvailabilityStatus::MissingBinary) {
        result.status = FormalVerificationStatus::CheckerUnavailable;
        result.error_message = checker.availability.reason.empty()
                                   ? "no compatible formal checker binary found"
                                   : checker.availability.reason;
        return result;
    }

    result.checker_path = checker.availability.binary_path;
    result.model_path = options.model_output_path.value_or(make_temp_model_path());
    result.model_path_retained = options.model_output_path.has_value();

    std::unordered_map<std::string, std::string> symbol_mappings;
    {
        if (options.model_output_path.has_value()) {
            const auto parent_path = result.model_path.parent_path();
            if (!parent_path.empty()) {
                std::error_code create_error;
                std::filesystem::create_directories(parent_path, create_error);
                if (create_error) {
                    result.error_message = "failed to create SMV model output directory";
                    return result;
                }
            }
        }

        std::ostringstream model_content;
        print_program_smv(program, model_content);
        const auto model_text = model_content.str();
        symbol_mappings = parse_symbol_mappings(model_text);
        result.expected_specification_count = count_expected_specifications(model_text);
        result.model_content = model_text;

        std::ofstream model(result.model_path);
        if (!model) {
            result.error_message = "failed to create temporary SMV model file";
            return result;
        }
        model << model_text;
    }

    const auto process =
        run_checker(result.checker_path, result.model_path, options.checker_timeout);
    result.exit_code = process.exit_code;
    result.output = process.output;
    result.checker_timed_out = process.timed_out;
    parse_checker_output(result, symbol_mappings);

    if (!result.model_path_retained) {
        std::error_code remove_error;
        std::filesystem::remove(result.model_path, remove_error);
    }

    if (!result.failing_specifications.empty()) {
        result.status = FormalVerificationStatus::Failed;

        if (options.explain) {
            const auto structured_mappings = parse_structured_symbol_mappings(result.model_content);
            const auto trace = parse_counterexample_trace(result.output, structured_mappings);
            if (trace.has_value()) {
                const auto explanation = explain_counterexample(*trace);
                result.structured_explanation_json = counterexample_to_json(*trace, explanation);
            }
        }

        return result;
    }

    if (result.checker_timed_out) {
        result.status = FormalVerificationStatus::CheckerError;
        result.availability_status = ModelCheckerAvailabilityStatus::Available;
        result.error_message = "formal checker process timed out after " +
                               std::to_string(result.checker_timeout.count()) + " second(s)";
        return result;
    }

    if (result.exit_code != 0) {
        result.status = FormalVerificationStatus::CheckerError;
        result.availability_status = ModelCheckerAvailabilityStatus::Available;
        result.error_message = "formal checker process returned a non-zero exit code";
        return result;
    }

    const auto observed_specification_count =
        result.proven_specifications.size() + result.failing_specifications.size();
    if (result.expected_specification_count > 0 && observed_specification_count == 0) {
        result.status = FormalVerificationStatus::CheckerError;
        result.availability_status = ModelCheckerAvailabilityStatus::Available;
        result.error_message = "formal checker did not emit any specification result";
        return result;
    }
    if (observed_specification_count < result.expected_specification_count) {
        result.status = FormalVerificationStatus::CheckerError;
        result.availability_status = ModelCheckerAvailabilityStatus::Available;
        result.error_message =
            "formal checker emitted fewer specification results than the generated SMV model";
        return result;
    }

    result.status = FormalVerificationStatus::Passed;
    return result;
#endif
}

void print_formal_verification_report(const FormalVerificationResult &result, std::ostream &out) {
    switch (result.status) {
    case FormalVerificationStatus::Passed:
        out << "ok: formal verification passed with " << result.proven_specifications.size()
            << " proven specification(s)\n";
        out << "formal_backend: " << model_checker_kind_name(result.backend_kind) << '\n';
        out << "checker_status: available\n";
        print_state_space_estimate(result.state_space_estimate, out);
        out << "checker: " << result.checker_path << '\n';
        if (result.model_path_retained) {
            out << "model: " << result.model_path.string() << '\n';
        }
        return;
    case FormalVerificationStatus::Failed:
        out << "error: formal verification failed with " << result.failing_specifications.size()
            << " failing specification(s)\n";
        out << "formal_backend: " << model_checker_kind_name(result.backend_kind) << '\n';
        out << "checker_status: available\n";
        print_state_space_estimate(result.state_space_estimate, out);
        out << "checker: " << result.checker_path << '\n';
        if (result.model_path_retained) {
            out << "model: " << result.model_path.string() << '\n';
        }
        for (const auto &specification : result.failing_specifications) {
            out << "failing: " << specification << '\n';
        }
        if (!result.counterexample_excerpt.empty()) {
            out << "counterexample excerpt:\n";
            for (const auto &line : result.counterexample_excerpt) {
                out << line << '\n';
            }
        }
        if (!result.counterexample_mappings.empty()) {
            out << "counterexample AHFL mapping:\n";
            for (const auto &mapping : result.counterexample_mappings) {
                out << mapping << '\n';
            }
        }
        if (result.structured_explanation_json.has_value()) {
            out << "counterexample explanation:\n";
            const auto &json = *result.structured_explanation_json;
            out << json;
            if (json.empty() || json.back() != '\n') {
                out << '\n';
            }
        }
        return;
    case FormalVerificationStatus::CheckerUnavailable:
        out << "error: formal checker unavailable";
        if (!result.error_message.empty()) {
            out << ": " << result.error_message;
        }
        out << '\n';
        out << "formal_backend: " << model_checker_kind_name(result.backend_kind) << '\n';
        out << "checker_status: missing_binary\n";
        print_state_space_estimate(result.state_space_estimate, out);
        if (!result.capabilities.required_binary.empty()) {
            out << "required_binary: " << result.capabilities.required_binary << '\n';
        }
        return;
    case FormalVerificationStatus::VerificationUnsupported:
        out << "error: formal verification unsupported";
        if (!result.error_message.empty()) {
            out << ": " << result.error_message;
        }
        out << '\n';
        out << "formal_backend: " << model_checker_kind_name(result.backend_kind) << '\n';
        out << "checker_status: verification_unsupported\n";
        print_state_space_estimate(result.state_space_estimate, out);
        if (!result.capabilities.required_binary.empty()) {
            out << "required_binary: " << result.capabilities.required_binary << '\n';
        }
        return;
    case FormalVerificationStatus::CheckerError:
        out << "error: formal checker failed";
        if (!result.error_message.empty()) {
            out << ": " << result.error_message;
        }
        out << '\n';
        out << "formal_backend: " << model_checker_kind_name(result.backend_kind) << '\n';
        out << "checker_status: checker_error\n";
        print_state_space_estimate(result.state_space_estimate, out);
        if (!result.checker_path.empty()) {
            out << "checker: " << result.checker_path << '\n';
        }
        out << "checker_timeout_seconds: " << result.checker_timeout.count() << '\n';
        if (result.checker_timed_out) {
            out << "checker_timed_out: true\n";
        }
        if (result.model_path_retained) {
            out << "model: " << result.model_path.string() << '\n';
        }
        if (result.exit_code != -1) {
            out << "exit_code: " << result.exit_code << '\n';
        }
#if defined(AHFL_ENABLE_BACKEND_FORMAL)
        if (!result.output.empty()) {
            out << "checker output:\n";
            print_output_excerpt(result.output, out);
        }
#endif
        return;
    }
}

} // namespace ahfl::formal
