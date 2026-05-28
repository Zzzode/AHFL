#include "ahfl/backends/driver.hpp"

#include "backends/pipeline/assurance.hpp"
#include "backends/pipeline/execution_plan.hpp"
#include "backends/pipeline/native_json.hpp"
#include "backends/pipeline/package_review.hpp"
#include "ahfl/backends/registry.hpp"
#include "ahfl/backends/smv.hpp"
#include "backends/pipeline/summary.hpp"
#include "ahfl/ir/ir.hpp"

#ifdef AHFL_ENABLE_BACKEND_INFRA
#include "backends/infra/lower.hpp"
#endif

namespace ahfl {

void initialize_builtin_backends(BackendRegistry &registry) {
    registry.register_backend(
        {BackendKind::Ir, "ir", "IR text format", [](const EmitContext &ctx) -> EmitResult {
             print_program_ir(ctx.program, ctx.out);
             return {};
         }});

    registry.register_backend(
        {BackendKind::IrJson, "ir-json", "IR JSON format", [](const EmitContext &ctx) -> EmitResult {
             print_program_ir_json(ctx.program, ctx.out);
             return {};
         }});

    registry.register_backend(
        {BackendKind::NativeJson, "native-json", "Native JSON handoff format",
         [](const EmitContext &ctx) -> EmitResult {
             if (ctx.package_metadata != nullptr) {
                 print_program_native_json(ctx.program, *ctx.package_metadata, ctx.out);
             } else {
                 print_program_native_json(ctx.program, ctx.out);
             }
             return {};
         }});

    registry.register_backend(
        {BackendKind::ExecutionPlan, "execution-plan", "Execution plan JSON",
         [](const EmitContext &ctx) -> EmitResult {
             if (ctx.package_metadata != nullptr) {
                 return print_program_execution_plan(ctx.program, *ctx.package_metadata, ctx.out);
             }
             return print_program_execution_plan(ctx.program, ctx.out);
         }});

    registry.register_backend(
        {BackendKind::PackageReview, "package-review", "Package review JSON",
         [](const EmitContext &ctx) -> EmitResult {
             if (ctx.package_metadata != nullptr) {
                 print_program_package_review(ctx.program, *ctx.package_metadata, ctx.out);
             } else {
                 print_program_package_review(ctx.program, ctx.out);
             }
             return {};
         }});

    registry.register_backend(
        {BackendKind::Summary, "summary", "Human-readable summary",
         [](const EmitContext &ctx) -> EmitResult {
             print_program_summary(ctx.program, ctx.out);
             return {};
         }});

    registry.register_backend(
        {BackendKind::Smv, "smv", "NuSMV model", [](const EmitContext &ctx) -> EmitResult {
             print_program_smv(ctx.program, ctx.out);
             return {};
         }});

    registry.register_backend(
        {BackendKind::AssuranceJson, "assurance-json", "Assurance validation JSON",
         [](const EmitContext &ctx) -> EmitResult {
             print_program_assurance_json(ctx.program, ctx.out);
             return {};
         }});

#ifdef AHFL_ENABLE_BACKEND_INFRA
    registry.register_backend(
        {BackendKind::InfraK8sCrd, "k8s-crd", "Kubernetes CRD YAML per agent",
         [](const EmitContext &ctx) -> EmitResult {
             auto configs = backends::lower_k8s_crd(ctx.program);
             if (configs.empty()) {
                 return std::unexpected<std::string>("no agents found in program");
             }
             for (const auto &c : configs) {
                 ctx.out << backends::generate_crd(c).yaml;
             }
             return {};
         }});

    registry.register_backend(
        {BackendKind::InfraOpenApi, "openapi", "OpenAPI spec from capabilities",
         [](const EmitContext &ctx) -> EmitResult {
             auto config = backends::lower_openapi(ctx.program);
             if (!config) {
                 return std::unexpected<std::string>("no capabilities found in program");
             }
             ctx.out << backends::generate_openapi(*config).json;
             return {};
         }});

    registry.register_backend(
        {BackendKind::InfraTerraform, "terraform", "Terraform HCL from workflows",
         [](const EmitContext &ctx) -> EmitResult {
             auto configs = backends::lower_terraform(ctx.program);
             if (configs.empty()) {
                 return std::unexpected<std::string>("no workflows found in program");
             }
             for (const auto &c : configs) {
                 ctx.out << backends::generate_terraform(c).hcl;
             }
             return {};
         }});

    registry.register_backend(
        {BackendKind::InfraWasm, "wasm", "WebAssembly WAT per agent",
         [](const EmitContext &ctx) -> EmitResult {
             auto configs = backends::lower_wasm(ctx.program);
             if (configs.empty()) {
                 return std::unexpected<std::string>("no agents found in program");
             }
             for (const auto &c : configs) {
                 ctx.out << backends::generate_wasm(c).wat_source;
             }
             return {};
         }});
#endif
}

EmitResult emit_backend(BackendKind kind,
                        const ir::Program &program,
                        std::ostream &out,
                        const handoff::PackageMetadata *package_metadata) {
    EmitContext ctx{program, out, package_metadata, {}};
    return global_backend_registry().emit(kind, ctx);
}

} // namespace ahfl
