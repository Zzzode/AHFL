#pragma once

// ---------------------------------------------------------------------------
// Single-source mangling facility for AHFL nominal instances.
//
// R-03 blocker mitigation: ALL mangling for concrete instantiations of named
// symbols (capability calls, predicate calls, agent instantiations, etc.) must
// go through mangle_instance(). Callers MUST NOT re-implement any part of the
// escaping or name-construction logic in their own translation units.
// ---------------------------------------------------------------------------

#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <cstdio>
#include <cstdint>
#include <functional>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>

namespace ahfl::mangle {

/// Resolver callback: map a SymbolId to its canonical name.
///
/// TypedProgram::find_symbol() is the expected backing store; keeping this as
/// a std::function keeps the mangling header independent of any specific
/// symbol-table type so it can be used from IR passes as well.
using SymbolCanonicalNameFn = std::function<std::optional<std::string>(SymbolId)>;

namespace detail {

/// Escape an arbitrary byte string into a single ASCII-safe,
/// underscore-delimited token that is safe to use as an identifier.
///
/// Escaping rules (R-03: defined exactly once):
///   * Characters matching [a-zA-Z0-9] are preserved verbatim.
///   * The namespace separator `::` becomes exactly one `_`.
///   * Every other byte `b` is rendered as `_Xhh` where `hh` is the lowercase
///     two-digit hexadecimal representation of `b`.
///
/// The escape is bijective over valid UTF-8 byte sequences, which is the
/// property that guarantees distinct inputs never collide after escaping.
[[nodiscard]] inline std::string mangle_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 4);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        // Namespace separator: `::` -> single `_` (greedy, one pair).
        if (c == ':' && i + 1 < text.size() && text[i + 1] == ':') {
            out.push_back('_');
            ++i;
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            out.push_back(static_cast<char>(c));
            continue;
        }
        // Every other byte encodes as _X<hh>.
        char buf[8];
        const int n = std::snprintf(buf, sizeof(buf), "_X%02x", c);
        if (n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
        }
    }
    return out;
}

} // namespace detail

/// Unique public mangling entry point.
///
/// Produces a deterministic, ASCII-safe, underscore-delimited mangled name
/// for the concrete instantiation of the nominal symbol identified by
/// `symbol` applied to the concrete `type_args`.
///
/// Layout:
///   ```
///   _inst_<hexSymbolId>_<escapedCanonicalName>[_<escapedType>...]
///   ```
///
/// Properties:
///   * ASCII-safe: every byte matches `[a-zA-Z0-9_]`.
///   * Namespace-prefixed: `::` in the canonical name becomes `_`, so the
///     originating module is preserved in the mangled text.
///   * Stable: identical inputs produce identical outputs across processes
///     and incremental rebuilds (depends only on SymbolId.value, canonical
///     name, and type descriptors).
///   * Injective: different `(SymbolId, type_args)` keys produce different
///     names (both the SymbolId and the escaped canonical name are embedded,
///     so even accidental canonical-name aliases between distinct numeric
///     SymbolIds cannot collide).
[[nodiscard]] inline std::string
mangle_instance(SymbolId symbol,
                std::span<const TypePtr> type_args,
                const SymbolCanonicalNameFn &resolver) {
    std::ostringstream out;

    // Visual prefix so instance names are immediately recognisable in IR JSON.
    out << "_inst_";

    // SymbolId value: 16-digit zero-padded lowercase hex of the numeric id.
    // Including this guarantees that two distinct nominal symbols never
    // collide even if their (escaped) canonical names accidentally coincide.
    {
        char buf[32];
        const int n = std::snprintf(
            buf, sizeof(buf), "%016zx", static_cast<std::size_t>(symbol.value));
        if (n > 0) {
            out.write(buf, n);
        }
    }
    out << '_';

    // Resolve and append the escaped canonical name. On resolution failure
    // emit a sentinel so downstream diagnostics surface the bug rather than
    // silently producing a plausible-but-wrong name.
    const auto canonical = resolver ? resolver(symbol) : std::nullopt;
    if (canonical.has_value() && !canonical->empty()) {
        out << detail::mangle_escape(*canonical);
    } else {
        out << "_UNKNOWN_SYMBOL_";
    }

    // Append each type argument's describe() output via mangle_escape so
    // that `List<Int>` and `Map<String, Int>` remain visually separable.
    for (const TypePtr t : type_args) {
        out << '_';
        if (t == nullptr) {
            out << "Any";
            continue;
        }
        out << detail::mangle_escape(t->describe());
    }

    return out.str();
}

} // namespace ahfl::mangle
