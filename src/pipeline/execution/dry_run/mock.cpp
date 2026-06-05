#include "base/json/json_value.hpp"
#include "pipeline/execution/dry_run/runner.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl::dry_run {

namespace {

void add_error(DiagnosticBag &diagnostics, std::string message) {
    diagnostics.error().message(std::move(message)).emit();
}

[[nodiscard]] bool starts_with_object(std::string_view content) {
    for (const auto ch : content) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            continue;
        }
        return ch == '{';
    }
    return false;
}

[[nodiscard]] std::optional<std::string> read_string_field(const json::JsonValue &field,
                                                           std::string_view message,
                                                           DiagnosticBag &diagnostics) {
    const auto value = field.as_string();
    if (!value.has_value()) {
        add_error(diagnostics, std::string(message));
        return std::nullopt;
    }
    return std::string(*value);
}

class CapabilityMockSetJsonReader final {
  public:
    explicit CapabilityMockSetJsonReader(const json::JsonValue &root, DiagnosticBag &diagnostics)
        : root_(root), diagnostics_(diagnostics) {}

    [[nodiscard]] std::optional<CapabilityMockSet> parse() {
        if (!root_.is_object()) {
            add_error(diagnostics_, "capability mock set must begin with '{'");
            return std::nullopt;
        }

        std::optional<std::string> format_version;
        std::vector<CapabilityMock> mocks;

        for (const auto &[key, value] : root_.object_fields) {
            if (key == "format_version") {
                format_version =
                    read_string_field(*value,
                                      "capability mock set field 'format_version' must be a string",
                                      diagnostics_);
            } else if (key == "mocks") {
                if (!read_mock_array(*value, mocks)) {
                    return std::nullopt;
                }
            } else {
                add_error(diagnostics_, "unsupported capability mock set field '" + key + "'");
                return std::nullopt;
            }

            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        if (!format_version.has_value()) {
            add_error(diagnostics_,
                      "capability mock set is missing required field 'format_version'");
        }
        if (diagnostics_.has_error()) {
            return std::nullopt;
        }

        if (*format_version != kCapabilityMockSetFormatVersion) {
            add_error(diagnostics_,
                      "unsupported capability mock set format_version '" + *format_version + "'");
            return std::nullopt;
        }

        validate_mock_semantics(mocks);
        if (diagnostics_.has_error()) {
            return std::nullopt;
        }

        return CapabilityMockSet{
            .format_version = std::move(*format_version),
            .mocks = std::move(mocks),
        };
    }

  private:
    const json::JsonValue &root_;
    DiagnosticBag &diagnostics_;

    void validate_mock_semantics(const std::vector<CapabilityMock> &mocks) {
        std::unordered_set<std::string> selectors;
        for (const auto &mock : mocks) {
            if (mock.capability_name.has_value() == mock.binding_key.has_value()) {
                add_error(diagnostics_,
                          "capability mock must specify exactly one of 'capability_name' or "
                          "'binding_key'");
                continue;
            }

            if (mock.capability_name.has_value() && mock.capability_name->empty()) {
                add_error(diagnostics_,
                          "capability mock field 'capability_name' must not be empty");
            }
            if (mock.binding_key.has_value() && mock.binding_key->empty()) {
                add_error(diagnostics_, "capability mock field 'binding_key' must not be empty");
            }
            if (mock.result_fixture.empty()) {
                add_error(diagnostics_, "capability mock field 'result_fixture' must not be empty");
            }

            const auto selector = mock.binding_key.has_value()
                                      ? "binding:" + *mock.binding_key
                                      : "capability:" + *mock.capability_name;
            if (!selectors.insert(selector).second) {
                add_error(diagnostics_,
                          "capability mock set contains duplicate mock selector '" + selector +
                              "'");
            }
        }
    }

    [[nodiscard]] std::optional<CapabilityMock> read_mock_object(const json::JsonValue &object) {
        if (!object.is_object()) {
            add_error(diagnostics_, "capability mock set field 'mocks[]' must be an object");
            return std::nullopt;
        }

        std::optional<std::string> capability_name;
        std::optional<std::string> binding_key;
        std::optional<std::string> result_fixture;
        std::optional<std::string> invocation_label;

        for (const auto &[key, value] : object.object_fields) {
            if (key == "capability_name") {
                capability_name =
                    read_string_field(*value,
                                      "capability mock field 'capability_name' must be a string",
                                      diagnostics_);
            } else if (key == "binding_key") {
                binding_key = read_string_field(
                    *value, "capability mock field 'binding_key' must be a string", diagnostics_);
            } else if (key == "result_fixture") {
                result_fixture =
                    read_string_field(*value,
                                      "capability mock field 'result_fixture' must be a string",
                                      diagnostics_);
            } else if (key == "invocation_label") {
                invocation_label =
                    read_string_field(*value,
                                      "capability mock field 'invocation_label' must be a string",
                                      diagnostics_);
            } else {
                add_error(diagnostics_, "unsupported capability mock field 'mocks[]." + key + "'");
                return std::nullopt;
            }

            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        if (!result_fixture.has_value()) {
            add_error(diagnostics_, "capability mock is missing required field 'result_fixture'");
            return std::nullopt;
        }

        return CapabilityMock{
            .capability_name = std::move(capability_name),
            .binding_key = std::move(binding_key),
            .result_fixture = std::move(*result_fixture),
            .invocation_label = std::move(invocation_label),
        };
    }

    [[nodiscard]] bool read_mock_array(const json::JsonValue &array,
                                       std::vector<CapabilityMock> &mocks) {
        if (!array.is_array()) {
            add_error(diagnostics_, "capability mock set field 'mocks' must be an array");
            return false;
        }

        for (const auto &item : array.array_items) {
            auto mock = read_mock_object(*item);
            if (!mock.has_value()) {
                return false;
            }
            mocks.push_back(std::move(*mock));
        }
        return true;
    }
};

} // namespace

CapabilityMockSetParseResult parse_capability_mock_set_json(std::string_view content) {
    CapabilityMockSetParseResult result{
        .mock_set = std::nullopt,
        .diagnostics = {},
    };

    auto root = json::parse_json(content);
    if (!root.has_value()) {
        add_error(result.diagnostics,
                  starts_with_object(content) ? "capability mock set must be valid JSON"
                                              : "capability mock set must begin with '{'");
        return result;
    }

    CapabilityMockSetJsonReader reader(**root, result.diagnostics);
    auto mock_set = reader.parse();
    if (!mock_set.has_value()) {
        return result;
    }

    result.mock_set = std::move(*mock_set);
    return result;
}

} // namespace ahfl::dry_run
