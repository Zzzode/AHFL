#include "runtime/providers/llm/prompt_template.hpp"

#include <string>

namespace ahfl::llm_provider {

PromptTemplate::PromptTemplate(std::string text) : text_(std::move(text)) {}

std::string
PromptTemplate::render(const std::unordered_map<std::string, std::string> &vars) const {
    std::string result;
    result.reserve(text_.size());

    std::size_t pos = 0;
    while (pos < text_.size()) {
        // Look for {{ marker
        auto start = text_.find("{{", pos);
        if (start == std::string::npos) {
            result += text_.substr(pos);
            break;
        }

        // Append text before {{
        result += text_.substr(pos, start - pos);

        // Find closing }}
        auto end = text_.find("}}", start + 2);
        if (end == std::string::npos) {
            // Malformed — append rest as-is
            result += text_.substr(start);
            break;
        }

        // Extract key (trim whitespace)
        auto key_start = start + 2;
        while (key_start < end && text_[key_start] == ' ') {
            ++key_start;
        }
        auto key_end = end;
        while (key_end > key_start && text_[key_end - 1] == ' ') {
            --key_end;
        }

        std::string key(text_.substr(key_start, key_end - key_start));
        auto it = vars.find(key);
        if (it != vars.end()) {
            result += it->second;
        } else {
            // Keep placeholder if variable not found
            result += text_.substr(start, end + 2 - start);
        }

        pos = end + 2;
    }

    return result;
}

void PromptTemplateRegistry::set_template(std::string_view cap_name, PromptTemplate tmpl) {
    templates_.insert_or_assign(std::string(cap_name), std::move(tmpl));
}

const PromptTemplate *PromptTemplateRegistry::get(std::string_view cap_name) const {
    auto it = templates_.find(std::string(cap_name));
    if (it == templates_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool PromptTemplateRegistry::has(std::string_view cap_name) const {
    return templates_.find(std::string(cap_name)) != templates_.end();
}

} // namespace ahfl::llm_provider
