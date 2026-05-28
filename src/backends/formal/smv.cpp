#include "backends/formal/smv_printer.hpp"

#include "ahfl/backends/smv.hpp"

namespace ahfl {

using namespace smv;
using namespace smv_detail;

void SmvPrinter::print(const ir::Program &program) {
    index_declarations(program);
    index_observations(program);
    collect_state_variables();
    collect_defines();
    collect_assignments();
    collect_specs();
    emit();
}

void SmvPrinter::emit() {
    out_ << "-- AHFL restricted SMV backend v0.1\n";
    out_ << "-- This lowering preserves validated state-machine, flow, and workflow "
            "structure.\n";
    out_ << "-- Bounded boolean/integer predicates are lowered directly; other data predicates "
            "remain environment observation assumptions.\n";
    out_ << "-- Capability calls/effects are bound to flow handler events where available.\n";
    for (const auto &mapping : symbol_mappings_) {
        out_ << "-- AHFL_MAP " << mapping << '\n';
    }
    out_ << "MODULE main\n";

    if (!state_variables_.empty()) {
        out_ << "VAR\n";
        for (const auto &state_var : state_variables_) {
            out_ << "  " << state_var << '\n';
        }
    }

    if (!observation_variables_.empty()) {
        out_ << "IVAR\n";
        for (const auto &observation : observation_variables_) {
            out_ << "  " << observation << '\n';
        }
    }

    if (!defines_.empty()) {
        out_ << "DEFINE\n";
        for (const auto &define : defines_) {
            out_ << "  " << define << '\n';
        }
    }

    if (!assignments_.empty()) {
        out_ << "ASSIGN\n";
        for (const auto &assignment : assignments_) {
            out_ << "  " << assignment << '\n';
        }
    }

    for (const auto &spec : specs_) {
        out_ << spec << '\n';
    }
}

void print_program_smv(const ir::Program &program, std::ostream &out) {
    SmvPrinter printer(out);
    printer.print(program);
}

void emit_program_smv(const ast::Program &program,
                      const ResolveResult &resolve_result,
                      const TypeCheckResult &type_check_result,
                      std::ostream &out) {
    print_program_smv(lower_program_ir(program, resolve_result, type_check_result), out);
}

} // namespace ahfl
