#include "formal/process_launcher.hpp"

#include "support/process.hpp"

namespace ahfl::formal {

ProcessResult launch_process(const ProcessConfig &config) {
    ahfl::support::ProcessConfig support_config;
    support_config.executable = config.executable;
    support_config.arguments = config.arguments;
    support_config.stdin_input = config.stdin_input;
    support_config.timeout = config.timeout;

    const auto result = ahfl::support::launch_process(support_config);
    return ProcessResult{
        .exit_code = result.exit_code,
        .stdout_output = result.stdout_output,
        .stderr_output = result.stderr_output,
        .timed_out = result.timed_out,
    };
}

std::optional<std::string> find_executable(std::string_view name) {
    return ahfl::support::find_executable(name);
}

} // namespace ahfl::formal
