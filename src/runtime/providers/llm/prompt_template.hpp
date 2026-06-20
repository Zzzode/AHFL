#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace ahfl::llm_provider {

/// A simple mustache-style prompt template: {{variable}} placeholders.
class PromptTemplate {
  public:
    explicit PromptTemplate(std::string text);

    /// Render the template by replacing {{key}} with vars[key].
    [[nodiscard]] std::string
    render(const std::unordered_map<std::string, std::string> &vars) const;

    [[nodiscard]] const std::string &text() const {
        return text_;
    }

  private:
    std::string text_;
};

/// Registry mapping capability names to custom prompt templates.
class PromptTemplateRegistry {
  public:
    void set_template(std::string_view cap_name, PromptTemplate tmpl);

    /// Get template for a capability. Returns nullptr if not set.
    [[nodiscard]] const PromptTemplate *get(std::string_view cap_name) const;

    [[nodiscard]] bool has(std::string_view cap_name) const;

  private:
    std::unordered_map<std::string, PromptTemplate> templates_;
};

} // namespace ahfl::llm_provider
