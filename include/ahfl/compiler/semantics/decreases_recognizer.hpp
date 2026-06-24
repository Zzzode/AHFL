#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/base/support/source.hpp"

namespace ahfl::sema {

// ============================================================================
// Decreases Pattern Recognizer — 4 MVP termination measure patterns
// ============================================================================
// The recognizer consumes a list of decreases-subject expressions (i.e. the
// comma-separated items that follow a `decreases ...` clause) and classifies each
// subterm into one of the supported MVP shapes:
//
//   1. LengthSelf   — length(self)   ①
//   2. SelfField    — self.<field>    ②  (commonly self.idx but any field)
//   3. MinusOne     — <ident> - 1    ③  (integer subtract-one shape
//   4. Wildcard     — *              ④  (decreases *; allowed only when
//                                              non-recursive and loop-free)
//
// Unknown / arbitrary expressions keep the `Unknown` classification so callers can issue
// diagnostics and defer to a later analysis.
// ============================================================================

/// Single classified pattern for one decreases subterm.
enum class DecreasePatternKind {
    LengthSelf,    // ① length(self)
    SelfField,     // ② self.<field>
    MinusOne,      // ③ <ident> - 1
    Wildcard,      // ④ wildcard marker
    Unknown,       // not a recognized MVP shape
};

[[nodiscard]] std::string_view to_string(DecreasePatternKind kind) noexcept;

// ---------------------------------------------------------------------------
// Pattern detail payloads (opaque payload: pattern metadata payloads for per-pattern metadata
// ---------------------------------------------------------------------------

/// LengthSelf pattern metadata.
struct PatternLengthSelf {
    SourceRange range;
    std::size_t subterm_index;  // position in the LexOrder
};

/// SelfField pattern metadata.
struct PatternSelfField {
    SourceRange range;
    std::size_t subterm_index;
    std::string field_name;   // the field accessed via "self" root +  eg "idx"
};

/// MinusOne pattern metadata.
struct PatternMinusOne {
    SourceRange range;
    std::size_t subterm_index;
    std::string identifier;  // name of the variable being decremented (e.g. "x" from "x - 1"
};

/// Wildcard pattern metadata.
struct PatternWildcard {
    SourceRange range;
    std::size_t subterm_index;
};

/// Unknown pattern — stores original source text for diagnostics.
struct PatternUnknown {
    SourceRange range;
    std::size_t subterm_index;
    std::string source_text;
};

/// Variant alias for classified subterm metadata.
using PatternDetail = std::variant<PatternLengthSelf,
                                   PatternSelfField,
                                   PatternMinusOne,
                                   PatternWildcard,
                                   PatternUnknown>;

/// A single entry in the lexicographic termination order.
struct LexOrderEntry {
    DecreasePatternKind kind;
    PatternDetail detail;
};

// ---------------------------------------------------------------------------
// Context flags used to validate legality for wildcard decreases *.
// ---------------------------------------------------------------------------

/// Structural context queried by call side about the containing definition context flags.
struct RecognizerContext {
    /// True if the enclosing callable is directly or transitively recursive
    /// (calls itself or participates in a recursive cycle).
    bool is_recursive{false};
    /// True if the enclosing flow handler contains any loop-like constructs
    /// (explicit iteration / while loops or recursive capability invocations that form a
    /// in-state cycle).
    bool contains_loop{false};
};

// ---------------------------------------------------------------------------
// LexOrder result — final recognizer output.
// ---------------------------------------------------------------------------

/// Result of recognizing a decreases clause (list of sub-terms + wildcard validity.
struct LexOrder {
    /// Ordered list of recognized subterm entries (preserves original source order.
    std::vector<LexOrderEntry> entries;

    /// True iff the clause had a wildcard `*` was the sole entry list contained a `decreases *` shape exactly.
    bool has_wildcard{false};

    /// Set to true iff a wildcard entry exists but the enclosing definition is
    /// recursive or loopful (wildcard was requested but not allowed).
    bool wildcard_illegal{false};

    /// Number of sub-terms that matched a recognized pattern.
    [[nodiscard]] std::size_t recognized_count() const noexcept;

    /// True iff all entries matched a non-Unknown pattern and (if wildcard is
    /// wildcard_was also_legal_and_valid (non_wildcard wildcard was used when allowed).
    [[nodiscard]] bool fully_recognized() const noexcept;
};

// ---------------------------------------------------------------------------
// Recognizer API.
// ---------------------------------------------------------------------------

/// Primary entry point: recognize the expressions from a decreases clause.
///
/// `subterms` is the list of expressions inside the decreases-list, in source order;
/// `context` carries recursive/loop metadata used for wildcard validation.
[[nodiscard]] LexOrder
recognize_decreases(std::vector<ast::ExprSyntax> const &subterms,
                  RecognizerContext const &context = {});

/// Helper: recognize a single sub-expression (useful when building LexOrderEntry useful for
/// testing and for incremental construction).
[[nodiscard]] LexOrderEntry
recognize_single(ast::ExprSyntax const &expr, std::size_t index);

}  // namespace ahfl::sema
