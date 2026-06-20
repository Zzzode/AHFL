#include "ahfl/compiler/semantics/name_suggestions.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace ahfl {

namespace {

[[nodiscard]] std::size_t edit_distance(std::string_view lhs, std::string_view rhs) {
    std::vector<std::size_t> previous(rhs.size() + 1);
    std::vector<std::size_t> current(rhs.size() + 1);
    for (std::size_t index = 0; index <= rhs.size(); ++index) {
        previous[index] = index;
    }

    for (std::size_t left = 1; left <= lhs.size(); ++left) {
        current[0] = left;
        for (std::size_t right = 1; right <= rhs.size(); ++right) {
            const auto substitution =
                previous[right - 1] + (lhs[left - 1] == rhs[right - 1] ? 0 : 1);
            current[right] = std::min({previous[right] + 1, current[right - 1] + 1, substitution});
        }
        std::swap(previous, current);
    }

    return previous[rhs.size()];
}

} // namespace

std::optional<std::string> suggest_name(std::string_view name,
                                        const std::vector<std::string> &candidates) {
    std::optional<std::string> best;
    auto best_distance = std::numeric_limits<std::size_t>::max();

    for (const auto &candidate : candidates) {
        const auto distance = edit_distance(name, candidate);
        if (distance > 2) {
            continue;
        }
        if (distance < best_distance ||
            (distance == best_distance && (!best.has_value() || candidate < *best))) {
            best_distance = distance;
            best = candidate;
        }
    }

    return best;
}

} // namespace ahfl
