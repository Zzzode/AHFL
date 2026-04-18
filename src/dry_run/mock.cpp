#include "ahfl/dry_run/runner.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl::dry_run {

namespace {

class CapabilityMockSetJsonParser final {
  public:
    explicit CapabilityMockSetJsonParser(std::string_view content, DiagnosticBag &diagnostics)
        : content_(content), diagnostics_(diagnostics) {}

    [[nodiscard]] std::optional<CapabilityMockSet> parse() {
        skip_whitespace();
        if (!consume('{')) {
            error_here("capability mock set must begin with '{'");
            return std::nullopt;
        }

        std::optional<std::string> format_version;
        std::vector<CapabilityMock> mocks;
        std::unordered_set<std::string> seen_fields;

        skip_whitespace();
        if (!consume('}')) {
            while (true) {
                auto key = parse_string("expected capability mock set field name");
                if (!key.has_value()) {
                    return std::nullopt;
                }

                if (!seen_fields.insert(*key).second) {
                    error_here("duplicate capability mock set field '" + *key + "'");
                    return std::nullopt;
                }

                if (!consume(':')) {
                    error_here("expected ':' after capability mock set field name");
                    return std::nullopt;
                }

                if (*key == "format_version") {
                    format_version =
                        parse_string("capability mock set field 'format_version' must be a string");
                } else if (*key == "mocks") {
                    if (!parse_mock_array(mocks)) {
                        return std::nullopt;
                    }
                } else {
                    error_here("unsupported capability mock set field '" + *key + "'");
                    return std::nullopt;
                }

                if (diagnostics_.has_error()) {
                    return std::nullopt;
                }

                skip_whitespace();
                if (consume('}')) {
                    break;
                }
                if (!consume(',')) {
                    error_here("expected ',' or '}' after capability mock set field");
                    return std::nullopt;
                }
            }
        }

        skip_whitespace();
        if (!at_end()) {
            error_here("capability mock set contains trailing content");
            return std::nullopt;
        }

        if (!format_version.has_value()) {
            diagnostics_.error("capability mock set is missing required field 'format_version'");
        }
        if (diagnostics_.has_error()) {
            return std::nullopt;
        }

        if (*format_version != kCapabilityMockSetFormatVersion) {
            diagnostics_.error("unsupported capability mock set format_version '" +
                               *format_version + "'");
            return std::nullopt;
        }

        std::unordered_set<std::string> selectors;
        for (const auto &mock : mocks) {
            if (mock.capability_name.has_value() == mock.binding_key.has_value()) {
                diagnostics_.error(
                    "capability mock must specify exactly one of 'capability_name' or 'binding_key'");
                continue;
            }

            if (mock.capability_name.has_value() && mock.capability_name->empty()) {
                diagnostics_.error("capability mock field 'capability_name' must not be empty");
            }
            if (mock.binding_key.has_value() && mock.binding_key->empty()) {
                diagnostics_.error("capability mock field 'binding_key' must not be empty");
            }
            if (mock.result_fixture.empty()) {
                diagnostics_.error("capability mock field 'result_fixture' must not be empty");
            }

            const auto selector = mock.binding_key.has_value()
                                      ? "binding:" + *mock.binding_key
                                      : "capability:" + *mock.capability_name;
            if (!selectors.insert(selector).second) {
                diagnostics_.error("capability mock set contains duplicate mock selector '" +
                                   selector + "'");
            }
        }

        if (diagnostics_.has_error()) {
            return std::nullopt;
        }

        return CapabilityMockSet{
            .format_version = std::move(*format_version),
            .mocks = std::move(mocks),
        };
    }

  private:
    std::string_view content_;
    DiagnosticBag &diagnostics_;
    std::size_t index_{0};

    [[nodiscard]] bool at_end() const noexcept {
        return index_ >= content_.size();
    }

    [[nodiscard]] char current() const noexcept {
        return at_end() ? '\0' : content_[index_];
    }

    void skip_whitespace() {
        while (!at_end()) {
            const auto ch = current();
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                break;
            }
            ++index_;
        }
    }

    [[nodiscard]] bool consume(char expected) {
        skip_whitespace();
        if (current() != expected) {
            return false;
        }

        ++index_;
        return true;
    }

    void error_here(std::string message) {
        diagnostics_.error(std::move(message));
    }

    [[nodiscard]] std::optional<std::string> parse_string(std::string_view missing_message) {
        skip_whitespace();
        if (current() != '"') {
            error_here(std::string(missing_message));
            return std::nullopt;
        }

        ++index_;
        std::string value;
        while (!at_end()) {
            const auto ch = content_[index_++];
            if (ch == '"') {
                return value;
            }

            if (ch == '\\') {
                if (at_end()) {
                    error_here("unterminated escape sequence in capability mock set string");
                    return std::nullopt;
                }

                const auto escaped = content_[index_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value.push_back(escaped);
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    error_here("unsupported escape sequence in capability mock set string");
                    return std::nullopt;
                }
                continue;
            }

            value.push_back(ch);
        }

        error_here("unterminated capability mock set string");
        return std::nullopt;
    }

    [[nodiscard]] bool parse_mock_object(CapabilityMock &mock) {
        if (!consume('{')) {
            error_here("capability mock set field 'mocks[]' must be an object");
            return false;
        }

        std::optional<std::string> capability_name;
        std::optional<std::string> binding_key;
        std::optional<std::string> result_fixture;
        std::optional<std::string> invocation_label;
        std::unordered_set<std::string> seen_fields;

        skip_whitespace();
        if (!consume('}')) {
            while (true) {
                auto key = parse_string("expected capability mock field name");
                if (!key.has_value()) {
                    return false;
                }

                if (!seen_fields.insert(*key).second) {
                    error_here("duplicate capability mock field 'mocks[]." + *key + "'");
                    return false;
                }

                if (!consume(':')) {
                    error_here("expected ':' after capability mock field name");
                    return false;
                }

                if (*key == "capability_name") {
                    capability_name =
                        parse_string("capability mock field 'capability_name' must be a string");
                } else if (*key == "binding_key") {
                    binding_key =
                        parse_string("capability mock field 'binding_key' must be a string");
                } else if (*key == "result_fixture") {
                    result_fixture =
                        parse_string("capability mock field 'result_fixture' must be a string");
                } else if (*key == "invocation_label") {
                    invocation_label =
                        parse_string("capability mock field 'invocation_label' must be a string");
                } else {
                    error_here("unsupported capability mock field 'mocks[]." + *key + "'");
                    return false;
                }

                if (diagnostics_.has_error()) {
                    return false;
                }

                skip_whitespace();
                if (consume('}')) {
                    break;
                }
                if (!consume(',')) {
                    error_here("expected ',' or '}' after capability mock field");
                    return false;
                }
            }
        }

        if (!result_fixture.has_value()) {
            diagnostics_.error("capability mock is missing required field 'result_fixture'");
            return false;
        }

        mock = CapabilityMock{
            .capability_name = std::move(capability_name),
            .binding_key = std::move(binding_key),
            .result_fixture = std::move(*result_fixture),
            .invocation_label = std::move(invocation_label),
        };
        return true;
    }

    [[nodiscard]] bool parse_mock_array(std::vector<CapabilityMock> &mocks) {
        if (!consume('[')) {
            error_here("capability mock set field 'mocks' must be an array");
            return false;
        }

        skip_whitespace();
        if (consume(']')) {
            return true;
        }

        while (true) {
            CapabilityMock mock;
            if (!parse_mock_object(mock)) {
                return false;
            }
            mocks.push_back(std::move(mock));

            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                error_here("expected ',' or ']' in capability mock array");
                return false;
            }
        }
    }
};

} // namespace

CapabilityMockSetParseResult parse_capability_mock_set_json(std::string_view content) {
    CapabilityMockSetParseResult result{
        .mock_set = std::nullopt,
        .diagnostics = {},
    };

    CapabilityMockSetJsonParser parser(content, result.diagnostics);
    auto mock_set = parser.parse();
    if (!mock_set.has_value()) {
        return result;
    }

    result.mock_set = std::move(*mock_set);
    return result;
}

} // namespace ahfl::dry_run
