// Self-contained mutation-testing target: a tiny canonical structured writer.
//
// This is target 2 of 3 for the fallback mutation runner (see
// arithmetic/arithmetic.hpp for target 1). Like that one it is deliberately
// small and dependency-free so run_fallback_mutation.sh can rebuild it in
// isolation. It models the class of helper the compiler relies on for stable
// text artifacts — a canonical JSON-ish writer, escaping and field emit — so
// the mutants here (field-separator placement, escape set, termination
// punctuation, closing punctuation) cover a genuinely different bug family
// from the arithmetic target's operator swaps.
//
// Its exact per-mutant outcomes are pinned in structured_writer.target.json.
#pragma once

#include <string>
#include <string_view>

// Escapes the characters that must not appear verbatim inside a JSON string
// body (RFC 8259: quote and backslash). Returns the string body without the
// surrounding quotes.
std::string escape(std::string_view text);

// Appends one canonical `"<key>": <value>` field to `out`. `first` is true for
// the first field of an object, in which case no separator is written.
void write_field(std::string& out, bool first,
                 std::string_view key, std::string_view value);

// Renders `{"<key>": "<value>"}` — the full one-field object, including the
// terminating newline.
std::string write_object(std::string_view key, std::string_view value);
