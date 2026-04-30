#include "ahfl/backends/driver.hpp"

#include "ahfl/backends/execution_plan.hpp"
#include "ahfl/backends/native_json.hpp"
#include "ahfl/backends/package_review.hpp"
#include "ahfl/backends/smv.hpp"
#include "ahfl/backends/summary.hpp"
#include "ahfl/ir/ir.hpp"

namespace ahfl {

void emit_backend(BackendKind kind,
                  const ir::Program &program,
                  std::ostream &out,
                  const handoff::PackageMetadata *package_metadata) {
    switch (kind) {
    case BackendKind::Ir:
        print_program_ir(program, out);
        return;
    case BackendKind::IrJson:
        print_program_ir_json(program, out);
        return;
    case BackendKind::NativeJson:
        if (package_metadata != nullptr) {
            print_program_native_json(program, *package_metadata, out);
            return;
        }
        print_program_native_json(program, out);
        return;
    case BackendKind::ExecutionPlan:
        if (package_metadata != nullptr) {
            print_program_execution_plan(program, *package_metadata, out);
            return;
        }
        print_program_execution_plan(program, out);
        return;
    case BackendKind::PackageReview:
        if (package_metadata != nullptr) {
            print_program_package_review(program, *package_metadata, out);
            return;
        }
        print_program_package_review(program, out);
        return;
    case BackendKind::Summary:
        print_program_summary(program, out);
        return;
    case BackendKind::Smv:
        print_program_smv(program, out);
        return;
    }
}

void emit_backend(BackendKind kind,
                  const ir::Program &program,
                  const ResolveResult &resolve_result,
                  const TypeCheckResult &type_check_result,
                  std::ostream &out,
                  const handoff::PackageMetadata *package_metadata) {
    static_cast<void>(resolve_result);
    static_cast<void>(type_check_result);
    emit_backend(kind, program, out, package_metadata);
}

void emit_backend(BackendKind kind,
                  const ast::Program &program,
                  const ResolveResult &resolve_result,
                  const TypeCheckResult &type_check_result,
                  std::ostream &out,
                  const handoff::PackageMetadata *package_metadata) {
    emit_backend(
        kind, lower_program_ir(program, resolve_result, type_check_result), out, package_metadata);
}

void emit_backend(BackendKind kind,
                  const SourceGraph &graph,
                  const ResolveResult &resolve_result,
                  const TypeCheckResult &type_check_result,
                  std::ostream &out,
                  const handoff::PackageMetadata *package_metadata) {
    emit_backend(
        kind, lower_program_ir(graph, resolve_result, type_check_result), out, package_metadata);
}

} // namespace ahfl
