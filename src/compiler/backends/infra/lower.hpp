#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "compiler/backends/infra/k8s_crd.hpp"
#include "compiler/backends/infra/openapi_spec.hpp"
#include "compiler/backends/infra/terraform_gen.hpp"

namespace ahfl::backends {

// RFC 0026 (KR6.3): the infra view backends consume the AHFL-IR
// (verification / orchestration) layer of the IR tower. `ir::AhflIr` is that
// layer; today it aliases `ir::Program`, so re-pointing these entry points is a
// zero-behavior-change annotation of the layer boundary.

/// Lower AHFL-IR to K8s CRD configs (one per AgentDecl).
/// An unexpected error carries a human-readable message (e.g. an agent port
/// type references a nominal the program does not declare), which the emitter
/// must surface instead of rendering a CRD with dangling $refs.
[[nodiscard]] std::expected<std::vector<K8sCrdConfig>, std::string>
lower_k8s_crd(const ir::AhflIr &program);

/// Lower AHFL-IR to an OpenAPI config (from the CapabilityDecl list).
/// Success with nullopt means the program declares no capabilities. An
/// unexpected error carries a human-readable message (e.g. a capability type
/// references a nominal the program does not declare), which the emitter must
/// surface instead of rendering a document with dangling $refs.
[[nodiscard]] std::expected<std::optional<OpenApiConfig>, std::string>
lower_openapi(const ir::AhflIr &program);

/// Lower AHFL-IR to Terraform configs (one per WorkflowDecl).
/// An unexpected error carries a human-readable message (e.g. a workflow node
/// references an agent the program does not declare), which the emitter must
/// surface instead of rendering a manifest with a dangling target.
[[nodiscard]] std::expected<std::vector<TerraformConfig>, std::string>
lower_terraform(const ir::AhflIr &program);

} // namespace ahfl::backends
