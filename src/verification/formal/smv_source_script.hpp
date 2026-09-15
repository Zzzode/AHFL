#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace ahfl::formal {

/// Verification engine selected when driving a NuSMV/nuXmv-compatible checker
/// through a batch source script.
enum class SmvCheckerEngine {
    /// Classic BDD engine: flatten / encode / build, then full LTL + invariant
    /// checks. Verdicts are unconditional ("is true" / "is false").
    Bdd,
    /// SAT-based bounded model checking: `go_bmc`, then bounded LTL and
    /// invariant checks unrolled up to `bmc_depth` transition steps.
    Bmc,
};

/// Builds the checker batch-source script text for the given model and engine.
///
/// BMC invariant checks are emitted as
/// `check_invar_bmc -a een-sorensson -k <K>`: on NuSMV 2.6.0 the `-k` bound is
/// rejected for the default ("classic") invariant algorithm
/// ("Option -k can be used only when een-sorensson algorithm is selected") and
/// is accepted only once een-sorensson is selected explicitly.
[[nodiscard]] std::string build_smv_source_script(const std::filesystem::path &model_path,
                                                  SmvCheckerEngine engine,
                                                  std::size_t bmc_depth);

/// Builds the script for (`engine`, `bmc_depth`) and writes it next to
/// `model_path`. Returns the script path, or an empty path when the file
/// cannot be created. The caller removes the returned file when done.
[[nodiscard]] std::filesystem::path write_smv_source_script(const std::filesystem::path &model_path,
                                                            SmvCheckerEngine engine,
                                                            std::size_t bmc_depth);

} // namespace ahfl::formal
