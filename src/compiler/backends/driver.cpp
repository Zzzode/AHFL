#include "ahfl/compiler/backends/driver.hpp"

#include "ahfl/compiler/backends/registry.hpp"
#include "compiler/backends/pipeline/assurance.hpp"
#include "compiler/backends/pipeline/execution_plan.hpp"
#include "compiler/backends/pipeline/native_json.hpp"
#include "compiler/backends/pipeline/package_review.hpp"

#ifdef AHFL_ENABLE_BACKEND_FORMAL
#include "ahfl/compiler/backends/smv.hpp"
#endif

#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "compiler/backends/pipeline/summary.hpp"

#ifdef AHFL_ENABLE_BACKEND_INFRA
#include "compiler/backends/infra/core_wasm_codegen.hpp"
#include "compiler/backends/infra/lower.hpp"
#endif

#include <limits>

namespace ahfl {

namespace {

[[nodiscard]] ir::ProgramPhase emission_analysis_phase(const ir::Program &program) noexcept {
    return program.phase == ir::ProgramPhase::Optimized ? ir::ProgramPhase::Optimized
                                                        : ir::ProgramPhase::Analyzed;
}

} // namespace

void initialize_builtin_backends(BackendRegistry &registry) {
    registry.register_builtin_backend(
        {BackendKind::Ir, "ir", "IR text format", [](const EmitContext &ctx) -> EmitResult {
             print_program_ir(ctx.program, ctx.out);
             return {};
         }});

    registry.register_builtin_backend({BackendKind::IrJson,
                                       "ir-json",
                                       "IR JSON format",
                                       [](const EmitContext &ctx) -> EmitResult {
                                           print_program_ir_json(ctx.program, ctx.out);
                                           return {};
                                       }});

    registry.register_builtin_backend({BackendKind::NativeJson,
                                       "native-json",
                                       "Native JSON handoff format",
                                       [](const EmitContext &ctx) -> EmitResult {
                                           if (ctx.package_metadata != nullptr) {
                                               print_program_native_json(
                                                   ctx.program, *ctx.package_metadata, ctx.out);
                                           } else {
                                               print_program_native_json(ctx.program, ctx.out);
                                           }
                                           return {};
                                       }});

    registry.register_builtin_backend(
        {BackendKind::ExecutionPlan,
         "execution-plan",
         "Execution plan JSON",
         [](const EmitContext &ctx) -> EmitResult {
             if (ctx.package_metadata != nullptr) {
                 return print_program_execution_plan(ctx.program, *ctx.package_metadata, ctx.out);
             }
             return print_program_execution_plan(ctx.program, ctx.out);
         }});

    registry.register_builtin_backend({BackendKind::PackageReview,
                                       "package-review",
                                       "Package review JSON",
                                       [](const EmitContext &ctx) -> EmitResult {
                                           if (ctx.package_metadata != nullptr) {
                                               print_program_package_review(
                                                   ctx.program, *ctx.package_metadata, ctx.out);
                                           } else {
                                               print_program_package_review(ctx.program, ctx.out);
                                           }
                                           return {};
                                       }});

    registry.register_builtin_backend({BackendKind::Summary,
                                       "summary",
                                       "Human-readable summary",
                                       [](const EmitContext &ctx) -> EmitResult {
                                           print_program_summary(ctx.program, ctx.out);
                                           return {};
                                       }});

#ifdef AHFL_ENABLE_BACKEND_FORMAL
    registry.register_builtin_backend(
        {BackendKind::Smv, "smv", "NuSMV model", [](const EmitContext &ctx) -> EmitResult {
             print_program_smv(ctx.program, ctx.out);
             return {};
         }});
#endif

    registry.register_builtin_backend({BackendKind::AssuranceJson,
                                       "assurance-json",
                                       "Assurance validation JSON",
                                       [](const EmitContext &ctx) -> EmitResult {
                                           print_program_assurance_json(ctx.program, ctx.out);
                                           return {};
                                       }});

#ifdef AHFL_ENABLE_BACKEND_INFRA
    registry.register_builtin_backend({BackendKind::InfraK8sCrd,
                                       "k8s-crd",
                                       "Kubernetes CRD YAML per agent",
                                       [](const EmitContext &ctx) -> EmitResult {
                                           auto configs = backends::lower_k8s_crd(ctx.program);
                                           if (configs.empty()) {
                                               return std::unexpected<std::string>(
                                                   "no agents found in program");
                                           }
                                           // Multi-agent programs emit one CRD
                                           // document per agent; separate them
                                           // with the YAML '---' directive so
                                           // the artifact is a valid multi-doc
                                           // YAML stream rather than
                                           // concatenated mappings.
                                           for (std::size_t i = 0; i < configs.size(); ++i) {
                                               if (i > 0) {
                                                   ctx.out << "---\n";
                                               }
                                               ctx.out << backends::generate_crd(configs[i]).yaml;
                                           }
                                           return {};
                                       }});

    registry.register_builtin_backend({BackendKind::InfraOpenApi,
                                       "openapi",
                                       "OpenAPI spec from capabilities",
                                       [](const EmitContext &ctx) -> EmitResult {
                                           auto config = backends::lower_openapi(ctx.program);
                                           if (!config) {
                                               return std::unexpected<std::string>(
                                                   "no capabilities found in program");
                                           }
                                           ctx.out << backends::generate_openapi(*config).json;
                                           return {};
                                       }});

    registry.register_builtin_backend({BackendKind::InfraTerraform,
                                       "terraform",
                                       "Terraform HCL from workflows",
                                       [](const EmitContext &ctx) -> EmitResult {
                                           auto configs = backends::lower_terraform(ctx.program);
                                           if (configs.empty()) {
                                               return std::unexpected<std::string>(
                                                   "no workflows found in program");
                                           }
                                           for (const auto &c : configs) {
                                               ctx.out << backends::generate_terraform(c).hcl;
                                           }
                                           return {};
                                       }});

    registry.register_builtin_backend({BackendKind::InfraWasm,
                                       "wasm",
                                       "WebAssembly binary from Core-IR",
                                       [](const EmitContext &ctx) -> EmitResult {
                                           auto core = ir::core::lower_ahfl_to_core(ctx.program);
                                           if (!core.ok()) {
                                               const auto &diag = core.diagnostics.front();
                                               return std::unexpected<std::string>(
                                                   diag.code + ": " + diag.message);
                                           }
                                           auto layouts =
                                               ir::core::compute_core_layouts(core.program);
                                           if (!layouts.ok()) {
                                               const auto &diag = layouts.diagnostics.front();
                                               return std::unexpected<std::string>(
                                                   diag.code + ": " + diag.message);
                                           }
                                           if (!layouts.table.has_value()) {
                                               return std::unexpected<std::string>(
                                                   "wasm.INVALID_LAYOUT: layout builder returned "
                                                   "no table without a diagnostic");
                                           }
                                           const auto profile =
                                               ctx.wasm_profile == WasmProfile::Browser
                                                   ? backends::WasmProfileKind::Browser
                                                   : backends::WasmProfileKind::Wasi;
                                           const auto entry = backends::resolve_core_wasm_entry(
                                               core.program, ctx.package_metadata);
                                           if (!entry.has_value()) {
                                               return std::unexpected<std::string>(
                                                   entry.error().code + ": " +
                                                   entry.error().message);
                                           }
                                           const auto emitted = backends::emit_core_wasm(
                                               core.program,
                                               *layouts.table,
                                               {*entry, profile});
                                           if (!emitted.ok()) {
                                               const auto &diag = emitted.diagnostics.front();
                                               return std::unexpected<std::string>(
                                                   diag.code + ": " + diag.message);
                                           }
                                           const auto &bytes = emitted.artifact->bytes;
                                           if (bytes.size() > static_cast<std::size_t>(
                                                                  std::numeric_limits<
                                                                      std::streamsize>::max())) {
                                               return std::unexpected<std::string>(
                                                   "wasm.BINARY_OVERFLOW: artifact exceeds "
                                                   "ostream write domain");
                                           }
                                           ctx.out.write(
                                               reinterpret_cast<const char *>(bytes.data()),
                                               static_cast<std::streamsize>(bytes.size()));
                                           if (!ctx.out) {
                                               return std::unexpected<std::string>(
                                                   "wasm.INTERNAL_INVALID: failed to write "
                                                   "complete artifact");
                                           }
                                           return {};
                                       }});
#endif
}

EmitResult emit_backend(BackendKind kind,
                        ir::Program &program,
                        std::ostream &out,
                        const handoff::PackageMetadata *package_metadata,
                        WasmProfile wasm_profile) {
    ir::ensure_derived_analyses(
        program, ir::all_derived_analysis_kinds(), emission_analysis_phase(program));
    EmitContext ctx{program, out, package_metadata, wasm_profile};
    return global_backend_registry().emit(kind, ctx);
}

} // namespace ahfl
