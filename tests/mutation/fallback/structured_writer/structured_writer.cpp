// Self-contained mutation-testing target implementation: canonical writer.
//
// The exact expression text below is what the fallback runner mutates via
// fixed, deterministic string substitutions. The mutant set itself is NOT
// here: it lives next to this file in structured_writer.target.json.
#include "structured_writer.hpp"

std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch == '"') {
            out += "\\\"";
        } else if (ch == '\\') {
            out += "\\\\";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

void write_field(std::string& out, bool first,
                 std::string_view key, std::string_view value) {
    if (!first) {
        out += ", ";
    }
    out += "\"";
    out += key;
    out += "\": \"";
    out += value;
    out += "\"";
}

std::string write_object(std::string_view key, std::string_view value) {
    std::string out = "{";
    write_field(out, true, key, value);
    out += "}";
    out += "\n";
    return out;
}
