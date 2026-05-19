#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ahfl::formal {

struct PropertyHash {
    std::string property_name;
    std::string content_hash;
};

struct IncrementalState {
    std::vector<PropertyHash> previous_hashes;
    std::vector<std::string> unchanged_properties;
    std::vector<std::string> changed_properties;
    std::vector<std::string> new_properties;
    std::vector<std::string> removed_properties;
};

class IncrementalVerifier {
public:
    [[nodiscard]] IncrementalState
    compute_diff(const std::vector<PropertyHash> &previous,
                 const std::vector<PropertyHash> &current);

    [[nodiscard]] static std::string
    hash_property(std::string_view property_text);
};

} // namespace ahfl::formal
