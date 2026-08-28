#pragma once

#include <optional>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "compiler/backends/infra/k8s_crd.hpp"
#include "compiler/backends/infra/openapi_spec.hpp"
#include "compiler/backends/infra/terraform_gen.hpp"
#include "compiler/backends/infra/wasm_backend.hpp"

namespace ahfl::backends {

// RFC 0026 (KR6.3): the infra view backends consume the AHFL-IR
// (verification / orchestration) layer of the IR tower. `ir::AhflIr` is that
// layer; today it aliases `ir::Program`, so re-pointing these entry points is a
// zero-behavior-change annotation of the layer boundary.

/// Lower AHFL-IR to K8s CRD configs (one per AgentDecl).
[[nodiscard]] std::vector<K8sCrdConfig> lower_k8s_crd(const ir::AhflIr &program);

/// Lower AHFL-IR to OpenAPI config (from CapabilityDecl list).
/// Returns nullopt if no capabilities found.
[[nodiscard]] std::optional<OpenApiConfig> lower_openapi(const ir::AhflIr &program);

/// Lower AHFL-IR to Terraform configs (one per WorkflowDecl).
[[nodiscard]] std::vector<TerraformConfig> lower_terraform(const ir::AhflIr &program);

/// Lower AHFL-IR to Wasm agent configs (one per AgentDecl).
[[nodiscard]] std::vector<WasmAgentConfig> lower_wasm(const ir::AhflIr &program);

} // namespace ahfl::backends
