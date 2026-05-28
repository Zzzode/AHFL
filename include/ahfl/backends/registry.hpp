#pragma once

#include <any>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/backends/driver.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

namespace ahfl {

/// Emission context passed to registered backend emitters.
struct EmitContext {
    const ir::Program &program;
    std::ostream &out;
    const handoff::PackageMetadata *package_metadata;
    std::any extension; // Backend-specific typed config
};

/// A registered backend entry.
struct BackendEntry {
    BackendKind kind;
    std::string name;          // e.g. "smv", "native-json"
    std::string description;   // Human-readable description
    std::function<EmitResult(const EmitContext &)> emitter;
};

/// Central registry of all available backends.
class BackendRegistry {
  public:
    BackendRegistry() = default;

    /// Register a new backend. Returns false if kind already registered.
    bool register_backend(BackendEntry entry);

    /// Emit using a registered backend.
    /// Returns an error if the kind is not registered or the backend fails.
    [[nodiscard]] EmitResult emit(BackendKind kind, const EmitContext &ctx) const;

    /// Look up a backend by kind.
    [[nodiscard]] const BackendEntry *find(BackendKind kind) const;

    /// Look up a backend by name string.
    [[nodiscard]] const BackendEntry *find_by_name(std::string_view name) const;

    /// List all registered backends.
    [[nodiscard]] const std::vector<BackendEntry> &entries() const { return entries_; }

    /// Get the number of registered backends.
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

  private:
    std::vector<BackendEntry> entries_;
};

/// Populate registry with all built-in backends.
void initialize_builtin_backends(BackendRegistry &registry);

/// Get the global backend registry (lazily initialized with built-in backends).
[[nodiscard]] BackendRegistry &global_backend_registry();

} // namespace ahfl
