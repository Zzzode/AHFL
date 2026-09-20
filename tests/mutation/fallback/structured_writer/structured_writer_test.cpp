// Narrow test suite for the structured-writer mutation target.
//
// Exit code 0 => all assertions hold. Any non-zero exit means the suite caught
// a defect; the fallback runner interprets that as "mutant killed".
//
// The suite deliberately leaves one behaviour unasserted — the exact
// backslash-escaping branch of escape() — so escape_pair survives and the
// reported score is an honest sub-100%, exactly as the arithmetic target does
// for scaled(). Every other assertion below exists to kill one specific
// mutant; the runner pins the resulting per-mutant outcome list in
// structured_writer.target.json, so each deliberate behaviour here is a
// reviewed, machine-checked baseline entry.
#include "structured_writer.hpp"

#include <cstdio>
#include <string>

int main() {
    int failures = 0;

    // Kills quote_escape: the quote must be escaped with a backslash; dropping
    // the backslash emits a bare quote and changes the output.
    if (escape("a\"b") != "a\\\"b") { ++failures; }

    // Kills field_separator: a non-first field is prefixed with ", "; any
    // other separator concatenates the fields differently.
    {
        std::string out = "{\"a\": \"1\"";
        write_field(out, false, "b", "2");
        if (out != "{\"a\": \"1\", \"b\": \"2\"") { ++failures; }
    }

    // Kills kv_separator: the canonical separator between an escaped key and
    // its quoted value is ": " (colon space), not ":".
    {
        std::string out;
        write_field(out, true, "k", "v");
        if (out != "\"k\": \"v\"") { ++failures; }
    }

    // Kills object_open_brace / object_close_brace / object_terminator: the
    // rendered one-field object is exactly `{"k": "v"}` plus the trailing
    // newline that makes it a complete output record.
    if (write_object("k", "v") != "{\"k\": \"v\"}\n") { ++failures; }

    // NOTE: the backslash branch of escape() is intentionally left unasserted
    // (escape_pair is the expected survivor).

    if (failures != 0) {
        std::printf("FAIL: %d assertion(s) failed\n", failures);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
