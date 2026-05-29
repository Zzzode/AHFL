#include "runtime/providers/llm/provider_registry.hpp"

#include <algorithm>

namespace ahfl::llm_provider {

void ProviderRegistry::add_provider(ProviderEntry entry) {
    providers_.push_back(std::move(entry));
}

std::optional<std::reference_wrapper<const ProviderEntry>> ProviderRegistry::select() const {
    const ProviderEntry *best = nullptr;

    for (const auto &p : providers_) {
        if (p.status == ProviderStatus::Unavailable) {
            continue;
        }
        if (best == nullptr || p.priority > best->priority) {
            best = &p;
        } else if (p.priority == best->priority && p.status == ProviderStatus::Available &&
                   best->status == ProviderStatus::Degraded) {
            best = &p;
        }
    }

    if (best == nullptr) {
        return std::nullopt;
    }
    return std::cref(*best);
}

void ProviderRegistry::mark_unavailable(std::string_view name) {
    for (auto &p : providers_) {
        if (p.name == name) {
            p.status = ProviderStatus::Unavailable;
            return;
        }
    }
}

void ProviderRegistry::mark_available(std::string_view name) {
    for (auto &p : providers_) {
        if (p.name == name) {
            p.status = ProviderStatus::Available;
            return;
        }
    }
}

} // namespace ahfl::llm_provider
