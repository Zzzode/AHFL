#include "runtime/providers/llm/provider_registry.hpp"

#include <algorithm>

namespace ahfl::llm_provider {
namespace {

[[nodiscard]] bool provider_precedes(const ProviderEntry &lhs, const ProviderEntry &rhs) {
    if (lhs.priority != rhs.priority) {
        return lhs.priority > rhs.priority;
    }
    if (lhs.status != rhs.status) {
        return lhs.status == ProviderStatus::Available;
    }
    return lhs.name < rhs.name;
}

} // namespace

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

std::vector<ProviderEntry> ProviderRegistry::selection_order() const {
    std::vector<ProviderEntry> candidates;
    for (const auto &provider : providers_) {
        if (provider.status != ProviderStatus::Unavailable) {
            candidates.push_back(provider);
        }
    }
    std::sort(candidates.begin(), candidates.end(), provider_precedes);
    return candidates;
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
