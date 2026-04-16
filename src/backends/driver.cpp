#include "ahfl/backends/driver.hpp"

#include "ahfl/backends/smv.hpp"
#include "ahfl/ir/ir.hpp"

namespace ahfl {

void emit_backend(BackendKind kind, const ir::Program &program, std::ostream &out) {
    switch (kind) {
    case BackendKind::Ir:
        print_program_ir(program, out);
        return;
    case BackendKind::IrJson:
        print_program_ir_json(program, out);
        return;
    case BackendKind::Smv:
        print_program_smv(program, out);
        return;
    }
}

void emit_backend(BackendKind kind,
                  const ast::Program &program,
                  const ResolveResult &resolve_result,
                  const TypeCheckResult &type_check_result,
                  std::ostream &out) {
    emit_backend(kind, lower_program_ir(program, resolve_result, type_check_result), out);
}

} // namespace ahfl
