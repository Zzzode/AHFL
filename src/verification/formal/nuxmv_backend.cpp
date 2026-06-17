#include "verification/formal/nuxmv_backend.hpp"

#include "verification/formal/process_launcher.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace ahfl::formal {

namespace {

namespace fs = std::filesystem;

/// Locate the nuXmv binary on PATH or via environment variable.
std::string find_nuxmv_binary() {
    // Check environment variable first
    const char *env = std::getenv("AHFL_NUXMV_PATH");
    if (env && *env && fs::exists(env)) {
        return env;
    }

    // Also check NuSMV as fallback
    const char *nusmv_env = std::getenv("AHFL_NUSMV_PATH");
    if (nusmv_env && *nusmv_env && fs::exists(nusmv_env)) {
        return nusmv_env;
    }

    // Search PATH for nuXmv or NuSMV
    const char *path_env = std::getenv("PATH");
    if (!path_env)
        return "";

    std::string path_str(path_env);
    std::istringstream iss(path_str);
    std::string dir;

    while (std::getline(iss, dir, ':')) {
        auto nuxmv_path = fs::path(dir) / "nuXmv";
        if (fs::exists(nuxmv_path)) {
            return nuxmv_path.string();
        }
        auto nuxmv_lower = fs::path(dir) / "nuxmv";
        if (fs::exists(nuxmv_lower)) {
            return nuxmv_lower.string();
        }
        auto nusmv_path = fs::path(dir) / "NuSMV";
        if (fs::exists(nusmv_path)) {
            return nusmv_path.string();
        }
        auto nusmv_lower = fs::path(dir) / "nusmv";
        if (fs::exists(nusmv_lower)) {
            return nusmv_lower.string();
        }
    }

    return "";
}

/// Write model text to a temporary file and return its path.
std::string write_temp_model(const std::string &model_text) {
    auto tmp_dir = fs::temp_directory_path() / "ahfl_verification_formal";
    fs::create_directories(tmp_dir);

    auto path = tmp_dir / "model.smv";
    std::ofstream ofs(path, std::ios::trunc);
    ofs << model_text;
    ofs.close();
    return path.string();
}

} // namespace

/// Parse nuXmv/NuSMV output for verification results.
/// Expected output contains lines like:
///   -- specification G (state != bad_state)  is true
///   -- specification F (state = target)  is false
///   -- as demonstrated by the following execution sequence
NuXmvVerificationOutput parse_nuxmv_verification_output(std::string_view output, bool timed_out) {
    NuXmvVerificationOutput parsed;
    if (timed_out) {
        parsed.status = NuXmvOutputStatus::Timeout;
        parsed.error = "nuXmv/NuSMV process timed out";
        return parsed;
    }

    std::istringstream iss{std::string(output)};
    std::string line;
    bool in_counterexample = false;

    while (std::getline(iss, line)) {
        // Check for property results
        if (line.find("-- specification") != std::string::npos ||
            line.find("-- LTL specification") != std::string::npos) {
            if (!parsed.counterexample_trace.empty() && in_counterexample) {
                in_counterexample = false;
            }

            parsed.properties_checked++;
            if (line.find("is true") != std::string::npos) {
                parsed.properties_passed++;
            } else if (line.find("is false") != std::string::npos) {
                in_counterexample = true;
            }
        }

        // Collect counterexample trace
        if (in_counterexample) {
            if (line.find("-> State:") != std::string::npos ||
                line.find("-> Input:") != std::string::npos ||
                line.find("state =") != std::string::npos) {
                parsed.counterexample_trace += line + "\n";
            }
        }

        // Check for errors
        if ((line.find("file") != std::string::npos && line.find("error") != std::string::npos) ||
            line.find("*** PARSE ERROR") != std::string::npos ||
            line.find("TYPE ERROR") != std::string::npos) {
            parsed.error += line + "\n";
        }
    }

    if (!parsed.error.empty()) {
        parsed.status = NuXmvOutputStatus::Error;
    } else if (parsed.properties_checked == 0) {
        parsed.status = NuXmvOutputStatus::Unknown;
    } else if (parsed.properties_passed == parsed.properties_checked) {
        parsed.status = NuXmvOutputStatus::Passed;
    } else {
        parsed.status = NuXmvOutputStatus::Failed;
    }

    return parsed;
}

[[nodiscard]] ModelCheckerKind NuXmvBackend::kind() const {
    return ModelCheckerKind::NuXmv;
}

[[nodiscard]] std::string_view NuXmvBackend::name() const {
    return "nuXmv";
}

[[nodiscard]] std::string_view NuXmvBackend::file_extension() const {
    return ".smv";
}

[[nodiscard]] ModelCheckerCapabilities NuXmvBackend::capabilities() const {
    return ModelCheckerCapabilities{
        .emits_model = true,
        .supports_external_verification = true,
        .supports_ahfl_smv_semantics = true,
        .required_binary = "nuXmv or NuSMV",
        .property_semantics =
            {
                "AHFL restricted SMV state-machine semantics",
                "workflow lifecycle LTLSPEC",
                "bounded control/data predicates",
                "capability call/effect event obligations",
            },
        .skip_reason =
            "nuXmv/NuSMV binary not found; pass --model-checker or set AHFL_SMV_CHECKER, "
            "AHFL_NUXMV_PATH, or AHFL_NUSMV_PATH",
    };
}

[[nodiscard]] ModelCheckerAvailability NuXmvBackend::availability() const {
    auto binary = find_nuxmv_binary();
    if (binary.empty()) {
        return ModelCheckerAvailability{
            .status = ModelCheckerAvailabilityStatus::MissingBinary,
            .binary_path = {},
            .reason = capabilities().skip_reason,
        };
    }
    return ModelCheckerAvailability{
        .status = ModelCheckerAvailabilityStatus::Available,
        .binary_path = std::move(binary),
        .reason = {},
    };
}

[[nodiscard]] ModelEmissionResult NuXmvBackend::emit_model(const BmcStateMachine &machine) {
    ModelEmissionResult result;

    if (machine.states.empty()) {
        result.success = false;
        result.error_message = "Cannot emit model: no states defined";
        return result;
    }

    std::string model;
    model += "MODULE main\n";

    // VAR declaration
    model += "VAR state : {";
    for (std::size_t i = 0; i < machine.states.size(); ++i) {
        if (i > 0) {
            model += ", ";
        }
        model += machine.states[i];
    }
    model += "};\n";

    // INIT
    model += "INIT state = " + machine.initial_state + ";\n";

    // TRANS
    model += "TRANS next(state) = case\n";
    for (const auto &t : machine.transitions) {
        model += "  state = " + t.from + " : " + t.to + ";\n";
    }
    model += "  TRUE : state;\nesac;\n";

    // Properties as LTLSPEC
    for (const auto &prop : machine.properties) {
        if (prop.size() > 6 && prop.substr(0, 6) == "never(") {
            auto bad_state = prop.substr(6, prop.size() - 7);
            model += "LTLSPEC G (state != " + bad_state + ");\n";
        } else if (prop.size() > 10 && prop.substr(0, 10) == "reachable(") {
            auto target = prop.substr(10, prop.size() - 11);
            model += "LTLSPEC F (state = " + target + ");\n";
        } else {
            model += "LTLSPEC " + prop + ";\n";
        }
    }

    result.success = true;
    result.model_text = std::move(model);
    return result;
}

[[nodiscard]] VerificationSummary NuXmvBackend::verify(const std::string &model_text) {
    VerificationSummary summary;

    // Find the nuXmv/NuSMV binary
    auto binary = find_nuxmv_binary();
    if (binary.empty()) {
        summary.all_passed = false;
        summary.properties_checked = 0;
        summary.properties_passed = 0;
        summary.error_message = "nuXmv/NuSMV binary not found. Set AHFL_NUXMV_PATH or ensure "
                                "nuXmv/NuSMV is on PATH.";
        return summary;
    }

    // Write model to temp file
    auto model_path = write_temp_model(model_text);

    // Build verification command
    // Use batch mode: nuXmv -source <cmd_file> <model_file>
    // Or simpler: pipe commands via stdin
    auto cmd_path = fs::temp_directory_path() / "ahfl_verification_formal" / "verify_cmd.txt";
    {
        std::ofstream cmd_file(cmd_path, std::ios::trunc);
        cmd_file << "read_model -i " << model_path << "\n";
        cmd_file << "flatten_hierarchy\n";
        cmd_file << "encode_variables\n";
        cmd_file << "build_model\n";
        cmd_file << "check_ltlspec\n";
        cmd_file << "quit\n";
    }

    ProcessConfig process_config;
    process_config.executable = binary;
    process_config.arguments = {"-source", cmd_path.string()};
    process_config.timeout = std::chrono::seconds{60};
    const auto process_result = launch_process(process_config);
    std::string output = process_result.stdout_output;
    if (!process_result.stderr_output.empty()) {
        output += process_result.stderr_output;
    }
    int exit_code = process_result.exit_code;

    // Clean up temp files
    std::error_code ec;
    fs::remove(model_path, ec);
    fs::remove(cmd_path, ec);

    // Parse the output
    auto parsed = parse_nuxmv_verification_output(output, process_result.timed_out);

    summary.properties_checked = parsed.properties_checked;
    summary.properties_passed = parsed.properties_passed;
    summary.timed_out = parsed.status == NuXmvOutputStatus::Timeout;
    summary.raw_output = output;

    if (parsed.status == NuXmvOutputStatus::Timeout) {
        summary.all_passed = false;
        summary.error_message = parsed.error;
    } else if (parsed.status == NuXmvOutputStatus::Error) {
        summary.all_passed = false;
        summary.error_message = parsed.error;
    } else if (parsed.properties_checked == 0 && exit_code != 0) {
        summary.all_passed = false;
        summary.error_message = "nuXmv exited with code " + std::to_string(exit_code);
        if (!output.empty()) {
            // Include first few lines of output for diagnostics
            std::istringstream oss(output);
            std::string line;
            int lines = 0;
            while (std::getline(oss, line) && lines < 5) {
                summary.error_message += "\n" + line;
                lines++;
            }
        }
    } else if (parsed.status == NuXmvOutputStatus::Unknown) {
        summary.all_passed = false;
        summary.error_message = "nuXmv produced no specification result";
    } else {
        summary.all_passed = parsed.status == NuXmvOutputStatus::Passed;
    }

    if (!parsed.counterexample_trace.empty()) {
        summary.error_message += "\nCounterexample:\n" + parsed.counterexample_trace;
    }

    return summary;
}

} // namespace ahfl::formal
