#include "ahfl/backends/registry.hpp"

#include <algorithm>

namespace ahfl {

BackendRegistry &BackendRegistry::instance() {
    static BackendRegistry registry;
    return registry;
}

bool BackendRegistry::register_backend(BackendEntry entry) {
    if (find(entry.kind) != nullptr) {
        return false;
    }
    entries_.push_back(std::move(entry));
    return true;
}

bool BackendRegistry::emit(BackendKind kind, const EmitContext &ctx) const {
    const auto *entry = find(kind);
    if (entry == nullptr) {
        return false;
    }
    entry->emitter(ctx);
    return true;
}

const BackendEntry *BackendRegistry::find(BackendKind kind) const {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [kind](const BackendEntry &e) { return e.kind == kind; });
    return it != entries_.end() ? &(*it) : nullptr;
}

const BackendEntry *BackendRegistry::find_by_name(std::string_view name) const {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [name](const BackendEntry &e) { return e.name == name; });
    return it != entries_.end() ? &(*it) : nullptr;
}

} // namespace ahfl
