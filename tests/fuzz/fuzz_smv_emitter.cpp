#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <variant>

#include "formal/bmc.hpp"
#include "formal/nuxmv_backend.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"

namespace {

void emit_smv_input(const uint8_t *data, std::size_t size) {
    if (data == nullptr || size == 0 || size > 1024 * 1024)
        return;
    std::string text(reinterpret_cast<const char *>(data), size);

    ahfl::Frontend frontend;
    auto parse_result = frontend.parse_text("fuzz_input", std::move(text));
    if (parse_result.has_errors() || !parse_result.program)
        return;

    ahfl::Resolver resolver;
    auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors())
        return;

    ahfl::TypeChecker checker;
    auto tc_result = checker.check(*parse_result.program, resolve_result);
    if (tc_result.has_errors())
        return;

    auto ir_program = ahfl::lower_program_ir(*parse_result.program, resolve_result, tc_result);

    for (const auto &decl : ir_program.declarations) {
        auto *agent = std::get_if<ahfl::ir::AgentDecl>(&decl);
        if (agent == nullptr)
            continue;

        ahfl::formal::BmcStateMachine machine;
        machine.name = agent->name;
        machine.states = agent->states;
        machine.initial_state = agent->initial_state;
        machine.final_states = agent->final_states;
        for (const auto &t : agent->transitions) {
            machine.transitions.push_back({t.from_state, t.to_state});
        }

        ahfl::formal::NuXmvBackend backend;
        auto emit_result = backend.emit_model(machine);
        volatile bool ok = emit_result.success;
        (void)ok;
    }
}

} // namespace

#ifdef AHFL_FUZZ_STANDALONE
int main() {
    std::printf("fuzz_smv_emitter standalone check\n");
    const char *valid = "agent Bot { states { idle, running } initial idle "
                        "transition idle -> running when start }";
    emit_smv_input(reinterpret_cast<const uint8_t *>(valid), std::strlen(valid));
    emit_smv_input(nullptr, 0);
    std::printf("  PASS: standalone smv_emitter fuzz check\n");
    return 0;
}
#else
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
    emit_smv_input(data, size);
    return 0;
}
#endif
