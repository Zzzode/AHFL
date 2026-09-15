#include "verification/formal/smv_source_script.hpp"

#include <fstream>
#include <string>

namespace ahfl::formal {

std::string build_smv_source_script(const std::filesystem::path &model_path,
                                    const SmvCheckerEngine engine,
                                    const std::size_t bmc_depth) {
    std::string script;
    script += "read_model -i " + model_path.string() + "\n";
    if (engine == SmvCheckerEngine::Bmc) {
        script += "go_bmc\n";
        script += "check_ltlspec_bmc -k " + std::to_string(bmc_depth) + "\n";
        // NuSMV 2.6.0 rejects -k for the default invariant algorithm; select
        // een-sorensson explicitly so the depth bound is honoured.
        script += "check_invar_bmc -a een-sorensson -k " + std::to_string(bmc_depth) + "\n";
    } else {
        script += "flatten_hierarchy\n";
        script += "encode_variables\n";
        script += "build_model\n";
        script += "check_ltlspec\n";
        script += "check_invar\n";
    }
    script += "quit\n";
    return script;
}

std::filesystem::path write_smv_source_script(const std::filesystem::path &model_path,
                                              const SmvCheckerEngine engine,
                                              const std::size_t bmc_depth) {
    const auto script_path =
        model_path.parent_path() / ("ahfl-smv-cmd-" + model_path.filename().string() + ".txt");
    std::ofstream script_file(script_path);
    if (!script_file) {
        return {};
    }
    script_file << build_smv_source_script(model_path, engine, bmc_depth);
    if (!script_file) {
        return {};
    }
    return script_path;
}

} // namespace ahfl::formal
