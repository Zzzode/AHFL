#pragma once

#include <optional>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "compiler/backends/infra/k8s_crd.hpp"
#include "compiler/backends/infra/openapi_spec.hpp"
#include "compiler/backends/infra/terraform_gen.hpp"
#include "compiler/backends/infra/wasm_backend.hpp"

namespace ahfl::backends {

/// Lower IR Program to K8s CRD configs (one per AgentDecl).
[[nodiscard]] std::vector<K8sCrdConfig> lower_k8s_crd(const ir::Program &program);

/// Lower IR Program to OpenAPI config (from CapabilityDecl list).
/// Returns nullopt if no capabilities found.
[[nodiscard]] std::optional<OpenApiConfig> lower_openapi(const ir::Program &program);

/// Lower IR Program to Terraform configs (one per WorkflowDecl).
[[nodiscard]] std::vector<TerraformConfig> lower_terraform(const ir::Program &program);

/// Lower IR Program to Wasm agent configs (one per AgentDecl).
[[nodiscard]] std::vector<WasmAgentConfig> lower_wasm(const ir::Program &program);

} // namespace ahfl::backends
