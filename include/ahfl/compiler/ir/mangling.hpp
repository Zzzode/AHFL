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
///   _inst_<hexContentHash>_<escapedCanonicalName>[_<escapedType>...]
///   ```
///
/// Properties:
///   * ASCII-safe: every byte matches `[a-zA-Z0-9_]`.
///   * Namespace-prefixed: `::` in the canonical name becomes `_`, so the
///     originating module is preserved in the mangled text.
///   * Stable across symbol-registration order: the hex prefix is a
///     deterministic FNV-1a 64-bit hash of (canonical name, type descriptors),
///     NOT the sequential SymbolId. Growing stdlib (or any unrelated symbol
///     registration) therefore no longer shifts instance names or churns IR
///     goldens. Identical (canonical, type_args) always hash identically
///     across processes and incremental rebuilds.
///   * Injective in practice: different (canonical name, type_args) keys
///     produce different hashes (64-bit collision-negligible). The IR data
///     model still keys instances by (SymbolId, type_args), so the mangled
///     name is a display/identity label, not the sole key.
[[nodiscard]] inline std::string
mangle_instance(SymbolId symbol,
                std::span<const TypePtr> type_args,
                const SymbolCanonicalNameFn &resolver) {
    // Resolve the canonical name first; together with the type args it uniquely
    // identifies the instance and is stable across symbol-registration order.
    const auto canonical = resolver ? resolver(symbol) : std::nullopt;

    // Build the content suffix: escaped canonical name + escaped type args.
    // On resolution failure emit a sentinel so downstream diagnostics surface
    // the bug rather than silently producing a plausible-but-wrong name.
    std::ostringstream suffix;
    if (canonical.has_value() && !canonical->empty()) {
        suffix << detail::mangle_escape(*canonical);
    } else {
        suffix << "_UNKNOWN_SYMBOL_";
    }
    for (const TypePtr t : type_args) {
        suffix << '_';
        if (t == nullptr) {
            suffix << "Any";
            continue;
        }
        suffix << detail::mangle_escape(t->describe());
    }
    const std::string content = suffix.str();

    // Stable identifier: FNV-1a 64-bit hash of the content suffix. This
    // replaces the old numeric SymbolId (a sequential registration counter) so
    // the mangled name no longer shifts when unrelated symbols are registered
    // — i.e. growing stdlib no longer churns IR goldens. The IR data model
    // still keys instances by (SymbolId, type_args), so this name remains a
    // display/identity label. See corelib-support-workplan §3.5 finding #5.
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const unsigned char c : content) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }

    std::ostringstream out;
    out << "_inst_";
    {
        char buf[32];
        const int n = std::snprintf(
            buf, sizeof(buf), "%016zx", static_cast<std::size_t>(h));
        if (n > 0) {
            out.write(buf, n);
        }
    }
    out << '_';
    out << content;
    return out.str();
}

} // namespace ahfl::mangle
