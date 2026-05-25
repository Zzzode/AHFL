#include "ahfl/backends/driver.hpp"

#include "backends/assurance.hpp"
#include "backends/execution_plan.hpp"
#include "backends/native_json.hpp"
#include "backends/package_review.hpp"
#include "ahfl/backends/registry.hpp"
#include "ahfl/backends/smv.hpp"
#include "backends/summary.hpp"
#include "ahfl/ir/ir.hpp"

namespace ahfl {

namespace {

// Self-registration of all built-in backends
const BackendRegistrar reg_ir{
    BackendEntry{BackendKind::Ir, "ir", "IR text format", [](const EmitContext &ctx) {
                     print_program_ir(ctx.program, ctx.out);
                 }}};

const BackendRegistrar reg_ir_json{
    BackendEntry{BackendKind::IrJson, "ir-json", "IR JSON format", [](const EmitContext &ctx) {
                     print_program_ir_json(ctx.program, ctx.out);
                 }}};

const BackendRegistrar reg_native_json{
    BackendEntry{BackendKind::NativeJson,
                 "native-json",
                 "Native JSON handoff format",
                 [](const EmitContext &ctx) {
                     if (ctx.package_metadata != nullptr) {
                         print_program_native_json(ctx.program, *ctx.package_metadata, ctx.out);
                     } else {
                         print_program_native_json(ctx.program, ctx.out);
                     }
                 }}};

const BackendRegistrar reg_execution_plan{
    BackendEntry{BackendKind::ExecutionPlan,
                 "execution-plan",
                 "Execution plan JSON",
                 [](const EmitContext &ctx) {
                     if (ctx.package_metadata != nullptr) {
                         print_program_execution_plan(ctx.program, *ctx.package_metadata, ctx.out);
                     } else {
                         print_program_execution_plan(ctx.program, ctx.out);
                     }
                 }}};

const BackendRegistrar reg_package_review{
    BackendEntry{BackendKind::PackageReview,
                 "package-review",
                 "Package review JSON",
                 [](const EmitContext &ctx) {
                     if (ctx.package_metadata != nullptr) {
                         print_program_package_review(ctx.program, *ctx.package_metadata, ctx.out);
                     } else {
                         print_program_package_review(ctx.program, ctx.out);
                     }
                 }}};

const BackendRegistrar reg_summary{BackendEntry{
    BackendKind::Summary, "summary", "Human-readable summary", [](const EmitContext &ctx) {
        print_program_summary(ctx.program, ctx.out);
    }}};

const BackendRegistrar reg_smv{
    BackendEntry{BackendKind::Smv, "smv", "NuSMV model", [](const EmitContext &ctx) {
                     print_program_smv(ctx.program, ctx.out);
                 }}};

const BackendRegistrar reg_assurance{BackendEntry{BackendKind::AssuranceJson,
                                                  "assurance-json",
                                                  "Assurance validation JSON",
                                                  [](const EmitContext &ctx) {
                                                      print_program_assurance_json(ctx.program,
                                                                                   ctx.out);
                                                  }}};

} // namespace

bool emit_backend(BackendKind kind,
                  const ir::Program &program,
                  std::ostream &out,
                  const handoff::PackageMetadata *package_metadata) {
    EmitContext ctx{program, out, package_metadata};
    return BackendRegistry::instance().emit(kind, ctx);
}

} // namespace ahfl
