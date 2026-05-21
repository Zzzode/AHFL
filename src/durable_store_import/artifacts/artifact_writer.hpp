#pragma once

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl::durable_store_import_artifacts_detail {

#define AHFL_ARTIFACT_PRINT_ENUM(value, ...)                                                       \
    do {                                                                                           \
        switch (value) { __VA_ARGS__; }                                                            \
    } while (false)

#define AHFL_ARTIFACT_ENUM_CASE(enumerator, wire_name)                                             \
    case enumerator:                                                                               \
        write_string(wire_name);                                                                   \
        return

#define AHFL_ARTIFACT_OSTREAM_ENUM_CASE(enumerator, wire_name)                                     \
    case enumerator:                                                                               \
        out << wire_name;                                                                          \
        return

class ArtifactJsonWriter : protected PrettyJsonWriter {
  protected:
    explicit ArtifactJsonWriter(std::ostream &out) : PrettyJsonWriter(out) {}

    using PrettyJsonWriter::print_array;
    using PrettyJsonWriter::print_object;
    using PrettyJsonWriter::write_string;

    void write_bool(bool value) {
        out_ << (value ? "true" : "false");
    }

    void write_null() {
        out_ << "null";
    }

    void print_optional_string(const std::optional<std::string> &value) {
        if (value.has_value()) {
            write_string(*value);
            return;
        }
        write_null();
    }

    template <typename Value, typename PrintValue>
    void print_optional(const std::optional<Value> &value, PrintValue print_value) {
        if (value.has_value()) {
            print_value(*value);
            return;
        }
        write_null();
    }
};

} // namespace ahfl::durable_store_import_artifacts_detail
