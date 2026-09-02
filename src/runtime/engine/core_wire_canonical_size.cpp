#include "runtime/engine/core_wire_canonical_size.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "base/support/json.hpp"

namespace ahfl::runtime::core_wire_canonical_size {
namespace {

using ahfl::ir::core::CoreWireSchemaDecimal;
using ahfl::ir::core::CoreWireSchemaDuration;
using ahfl::ir::core::CoreWireSchemaEnum;
using ahfl::ir::core::CoreWireSchemaMap;
using ahfl::ir::core::CoreWireSchemaNode;
using ahfl::ir::core::CoreWireSchemaNodeId;
using ahfl::ir::core::CoreWireSchemaOption;
using ahfl::ir::core::CoreWireSchemaSequence;
using ahfl::ir::core::CoreWireSchemaShape;
using ahfl::ir::core::CoreWireSchemaString;
using ahfl::ir::core::CoreWireSchemaStruct;
using ahfl::ir::core::CoreWireSchemaTuple;
using ahfl::ir::core::CoreWirePayloadKind;

// Checked u64 arithmetic (mirrors src/compiler/ir/core_layout.cpp). A nullopt is a
// size overflow, mapped by the caller to SizeOverflow.
[[nodiscard]] std::optional<std::uint64_t> checked_add(std::uint64_t lhs, std::uint64_t rhs) {
    if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs) {
        return std::nullopt;
    }
    return lhs + rhs;
}
[[nodiscard]] std::optional<std::uint64_t> checked_mul(std::uint64_t lhs, std::uint64_t rhs) {
    if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return std::nullopt;
    }
    return lhs * rhs;
}
// acc += v, in place; false on overflow.
[[nodiscard]] bool add_into(std::uint64_t &acc, std::uint64_t v) {
    const auto sum = checked_add(acc, v);
    if (!sum) {
        return false;
    }
    acc = *sum;
    return true;
}

// E(s): the REAL escaped, quoted byte length of a FIXED schema-owned name (struct
// / field / enum / variant wire names, and the fixed discriminator keys). Goes
// through the canonical serializer's escape SSOT verbatim — never approximated as
// 6*len+2 (that approximation is only for BOUNDED DYNAMIC content, see S()).
[[nodiscard]] std::uint64_t escaped_name_len(std::string_view name) {
    std::ostringstream oss;
    ahfl::write_escaped_json_string(oss, name);
    return static_cast<std::uint64_t>(oss.str().size());
}

// S(B): the checked worst-case byte bound for BOUNDED DYNAMIC String content (a
// String value or a Map key value) of at most B UTF-8 bytes = 2 quotes + 6 bytes
// per input byte (write_escaped_json_string emits at most `\u00xx` = 6 for any
// single byte, and passes multi-byte UTF-8 through 1:1). nullopt on overflow.
[[nodiscard]] std::optional<std::uint64_t> string_content_bound(std::uint64_t max_bytes) {
    const auto six = checked_mul(6, max_bytes);
    if (!six) {
        return std::nullopt;
    }
    return checked_add(2, *six);
}

// A schema node is INTRINSICALLY unbounded when no schema field can cap its
// canonical spelling: a String with no length_bounds, a Decimal or Duration
// (spelling preserved verbatim, unbounded leading zeros), or a Sequence/Set/Map
// with no capacity.
[[nodiscard]] bool intrinsic_unbounded(const CoreWireSchemaShape &shape) {
    if (const auto *s = std::get_if<CoreWireSchemaString>(&shape)) {
        return !s->length_bounds.has_value();
    }
    if (std::holds_alternative<CoreWireSchemaDecimal>(shape) ||
        std::holds_alternative<CoreWireSchemaDuration>(shape)) {
        return true;
    }
    if (const auto *seq = std::get_if<CoreWireSchemaSequence>(&shape)) {
        return !seq->capacity.has_value();
    }
    if (const auto *m = std::get_if<CoreWireSchemaMap>(&shape)) {
        return !m->capacity.has_value();
    }
    return false;
}

// Invoke `visit(childId)` for each PRODUCTIVE child edge of `shape`: Option /
// Struct / Enum / Tuple children are always productive; a Sequence/Set element
// and a Map key+value are productive ONLY when a non-zero capacity can instantiate
// them (capacity 0 cuts the edge; capacity absent is caught as intrinsic_unbounded
// before enumeration). Scalars have no children.
template <typename F> void for_each_productive_child(const CoreWireSchemaShape &shape, F &&visit) {
    if (const auto *opt = std::get_if<CoreWireSchemaOption>(&shape)) {
        visit(opt->value);
    } else if (const auto *seq = std::get_if<CoreWireSchemaSequence>(&shape)) {
        if (seq->capacity.has_value() && *seq->capacity > 0) {
            visit(seq->element);
        }
    } else if (const auto *m = std::get_if<CoreWireSchemaMap>(&shape)) {
        if (m->capacity.has_value() && *m->capacity > 0) {
            visit(m->key);
            visit(m->value);
        }
    } else if (const auto *st = std::get_if<CoreWireSchemaStruct>(&shape)) {
        for (const auto &field : st->fields) {
            visit(field.type);
        }
    } else if (const auto *en = std::get_if<CoreWireSchemaEnum>(&shape)) {
        for (const auto &variant : en->variants) {
            for (const auto &slot : variant.slots) {
                visit(slot.type);
            }
        }
    } else if (const auto *tup = std::get_if<CoreWireSchemaTuple>(&shape)) {
        for (const auto &elem : tup->elements) {
            visit(elem);
        }
    }
}

enum class Color : std::uint8_t { White, Gray, Black };

// An explicit DFS frame: an enter visit (`is_exit == false`) discovers a node and
// schedules its children; an exit visit (`is_exit == true`) finalizes it after the
// whole subtree. Using markers keeps the traversal iterative (no C++ recursion),
// so an arbitrarily deep legal chain cannot exhaust the stack.
struct Frame {
    CoreWireSchemaNodeId node;
    bool is_exit;
};

// Pass 1: prove the productive subgraph reachable from `root` is finite and
// acyclic. Returns Unbounded on the first intrinsically-unbounded node or the
// first productive back-edge (a Gray target = an ancestor on the current path).
// Fails closed to Unbounded on any out-of-range id (cannot occur for a Verified
// binding; we refuse to promise a finite bound we cannot prove).
[[nodiscard]] std::optional<MaxCanonicalSizeError>
check_bounded(const std::vector<CoreWireSchemaNode> &nodes, CoreWireSchemaNodeId root) {
    const std::size_t n = nodes.size();
    std::vector<Color> color(n, Color::White);
    std::vector<Frame> stack;
    stack.push_back({root, false});
    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        if (frame.node.value >= n) {
            return MaxCanonicalSizeError::Unbounded;
        }
        if (frame.is_exit) {
            color[frame.node.value] = Color::Black;
            continue;
        }
        if (color[frame.node.value] != Color::White) {
            continue; // already entered via another edge
        }
        color[frame.node.value] = Color::Gray;
        const auto &shape = nodes[frame.node.value].shape;
        if (intrinsic_unbounded(shape)) {
            return MaxCanonicalSizeError::Unbounded;
        }
        stack.push_back({frame.node, true}); // exit marker, below the children
        bool unbounded = false;
        for_each_productive_child(shape, [&](CoreWireSchemaNodeId child) {
            if (unbounded) {
                return;
            }
            if (child.value >= n || color[child.value] == Color::Gray) {
                unbounded = true; // out-of-range OR productive back-edge (cycle)
                return;
            }
            if (color[child.value] == Color::White) {
                stack.push_back({child, false});
            }
        });
        if (unbounded) {
            return MaxCanonicalSizeError::Unbounded;
        }
    }
    return std::nullopt;
}

// The size of one node given its already-computed child sizes. Called only after
// check_bounded proved the graph finite, so every productive child is memoized and
// no intrinsically-unbounded shape is reachable; the defensive Unbounded returns
// below cannot fire in practice (they never underestimate if they somehow did).
[[nodiscard]] std::expected<std::uint64_t, MaxCanonicalSizeError>
size_of(const CoreWireSchemaShape &shape, const std::vector<std::optional<std::uint64_t>> &memo,
        std::size_t n) {
    // Read a memoized child size (fail closed if absent / out of range).
    const auto child = [&](CoreWireSchemaNodeId id) -> std::optional<std::uint64_t> {
        if (id.value >= n || !memo[id.value].has_value()) {
            return std::nullopt;
        }
        return memo[id.value];
    };
    const auto overflow = std::unexpected(MaxCanonicalSizeError::SizeOverflow);
    const auto unbounded = std::unexpected(MaxCanonicalSizeError::Unbounded);

    if (std::holds_alternative<ahfl::ir::core::CoreWireSchemaUnit>(shape)) {
        return std::uint64_t{4}; // null
    }
    if (std::holds_alternative<ahfl::ir::core::CoreWireSchemaBool>(shape)) {
        return std::uint64_t{5}; // false
    }
    if (std::holds_alternative<ahfl::ir::core::CoreWireSchemaInt>(shape)) {
        return std::uint64_t{20}; // -9223372036854775808 (fixed, conservative)
    }
    if (std::holds_alternative<ahfl::ir::core::CoreWireSchemaFloat>(shape)) {
        return std::uint64_t{24}; // %.17g general, e.g. -1.7976931348623157e+308
    }
    if (std::holds_alternative<ahfl::ir::core::CoreWireSchemaTimestamp>(shape)) {
        return std::uint64_t{35}; // {"_timestamp":<i64>}
    }
    if (std::holds_alternative<ahfl::ir::core::CoreWireSchemaUuid>(shape)) {
        return std::uint64_t{44}; // {"_uuid":"<32 hex>"}
    }
    if (const auto *s = std::get_if<CoreWireSchemaString>(&shape)) {
        if (!s->length_bounds.has_value()) {
            return unbounded; // defensive (pass 1 already returned Unbounded)
        }
        const auto bound =
            string_content_bound(static_cast<std::uint64_t>(s->length_bounds->second));
        return bound ? std::expected<std::uint64_t, MaxCanonicalSizeError>(*bound) : overflow;
    }
    if (std::holds_alternative<CoreWireSchemaDecimal>(shape) ||
        std::holds_alternative<CoreWireSchemaDuration>(shape)) {
        return unbounded; // defensive (verbatim spelling has no cap)
    }
    if (const auto *opt = std::get_if<CoreWireSchemaOption>(&shape)) {
        const auto inner = child(opt->value);
        if (!inner) {
            return unbounded;
        }
        return std::max<std::uint64_t>(4, *inner); // None=null=4, Some(x)=x verbatim
    }
    if (const auto *seq = std::get_if<CoreWireSchemaSequence>(&shape)) {
        if (!seq->capacity.has_value()) {
            return unbounded;
        }
        const std::uint64_t cap = *seq->capacity;
        if (cap == 0) {
            return std::uint64_t{2}; // []
        }
        const auto elem = child(seq->element);
        if (!elem) {
            return unbounded;
        }
        const auto body = checked_mul(cap, *elem);
        if (!body) {
            return overflow;
        }
        std::uint64_t total = 2; // [ ]
        if (!add_into(total, *body) || !add_into(total, cap - 1)) { // element commas
            return overflow;
        }
        return total;
    }
    if (const auto *m = std::get_if<CoreWireSchemaMap>(&shape)) {
        if (!m->capacity.has_value()) {
            return unbounded;
        }
        const std::uint64_t cap = *m->capacity;
        if (cap == 0) {
            return std::uint64_t{2}; // {}
        }
        const auto key = child(m->key);
        const auto value = child(m->value);
        if (!key || !value) {
            return unbounded;
        }
        std::uint64_t per_entry = *key; // "key"
        if (!add_into(per_entry, 1) || !add_into(per_entry, *value)) { // : value
            return overflow;
        }
        const auto body = checked_mul(cap, per_entry);
        if (!body) {
            return overflow;
        }
        std::uint64_t total = 2; // { }
        if (!add_into(total, *body) || !add_into(total, cap - 1)) { // entry commas
            return overflow;
        }
        return total;
    }
    if (const auto *st = std::get_if<CoreWireSchemaStruct>(&shape)) {
        std::uint64_t total = 1; // {
        if (!add_into(total, escaped_name_len("_type")) || !add_into(total, 1) || // :
            !add_into(total, escaped_name_len(st->wire_name))) {
            return overflow;
        }
        for (const auto &field : st->fields) {
            const auto ft = child(field.type);
            if (!ft) {
                return unbounded;
            }
            if (!add_into(total, 1) ||                                  // ,
                !add_into(total, escaped_name_len(field.wire_name)) ||  // "name"
                !add_into(total, 1) ||                                  // :
                !add_into(total, *ft)) {
                return overflow;
            }
        }
        if (!add_into(total, 1)) { // }
            return overflow;
        }
        return total;
    }
    if (const auto *en = std::get_if<CoreWireSchemaEnum>(&shape)) {
        // A conforming value is exactly ONE variant, so the bound is the MAX over
        // variants of the fixed enum frame + that variant's payload.
        std::uint64_t base = 1; // {
        if (!add_into(base, escaped_name_len("_enum")) || !add_into(base, 1) ||   // :
            !add_into(base, escaped_name_len(en->wire_name)) || !add_into(base, 1) || // ,
            !add_into(base, escaped_name_len("_variant")) || !add_into(base, 1)) { // :
            return overflow;
        }
        std::uint64_t best = 0;
        for (const auto &variant : en->variants) {
            std::uint64_t total = base;
            if (!add_into(total, escaped_name_len(variant.wire_name))) {
                return overflow;
            }
            // Payload term. An empty Tuple/Struct payload emits NO wrapper, so it
            // adds exactly 0 (matching value_to_json's omission).
            if (variant.payload_kind == CoreWirePayloadKind::Tuple && !variant.slots.empty()) {
                if (!add_into(total, 1) ||                                  // ,
                    !add_into(total, escaped_name_len("_payload")) ||       // "_payload"
                    !add_into(total, 1) || !add_into(total, 1)) {           // : [
                    return overflow;
                }
                for (const auto &slot : variant.slots) {
                    const auto ss = child(slot.type);
                    if (!ss) {
                        return unbounded;
                    }
                    if (!add_into(total, *ss)) {
                        return overflow;
                    }
                }
                if (!add_into(total, variant.slots.size() - 1) || // element commas
                    !add_into(total, 1)) {                        // ]
                    return overflow;
                }
            } else if (variant.payload_kind == CoreWirePayloadKind::Struct &&
                       !variant.slots.empty()) {
                if (!add_into(total, 1) ||                                    // ,
                    !add_into(total, escaped_name_len("_named_payload")) ||   // "_named_payload"
                    !add_into(total, 1) || !add_into(total, 1)) {             // : {
                    return overflow;
                }
                for (const auto &slot : variant.slots) {
                    const auto ss = child(slot.type);
                    if (!ss) {
                        return unbounded;
                    }
                    if (!add_into(total, escaped_name_len(slot.wire_name)) || // "name"
                        !add_into(total, 1) ||                                // :
                        !add_into(total, *ss)) {
                        return overflow;
                    }
                }
                if (!add_into(total, variant.slots.size() - 1) || // entry commas
                    !add_into(total, 1)) {                        // }
                    return overflow;
                }
            }
            if (!add_into(total, 1)) { // }
                return overflow;
            }
            best = std::max(best, total);
        }
        return best;
    }
    if (const auto *tup = std::get_if<CoreWireSchemaTuple>(&shape)) {
        const std::size_t count = tup->elements.size();
        if (count == 0) {
            return std::uint64_t{2}; // []
        }
        std::uint64_t total = 2; // [ ]
        for (const auto &elem : tup->elements) {
            const auto es = child(elem);
            if (!es) {
                return unbounded;
            }
            if (!add_into(total, *es)) {
                return overflow;
            }
        }
        if (!add_into(total, static_cast<std::uint64_t>(count) - 1)) { // element commas
            return overflow;
        }
        return total;
    }
    return unbounded; // unreachable: every shape is handled above
}

// Pass 2: compute the memoized size over the proven-finite productive DAG in
// child-before-parent (DFS finish) order. Iterative; no recursion.
[[nodiscard]] std::expected<std::uint64_t, MaxCanonicalSizeError>
compute_size(const std::vector<CoreWireSchemaNode> &nodes, CoreWireSchemaNodeId root) {
    const std::size_t n = nodes.size();
    std::vector<std::optional<std::uint64_t>> memo(n);
    std::vector<Frame> stack;
    stack.push_back({root, false});
    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        if (frame.node.value >= n) {
            return std::unexpected(MaxCanonicalSizeError::Unbounded); // fail closed
        }
        if (frame.is_exit) {
            if (memo[frame.node.value].has_value()) {
                continue; // computed via another edge
            }
            auto sized = size_of(nodes[frame.node.value].shape, memo, n);
            if (!sized) {
                return sized;
            }
            memo[frame.node.value] = *sized;
            continue;
        }
        if (memo[frame.node.value].has_value()) {
            continue;
        }
        stack.push_back({frame.node, true}); // exit marker, below the children
        for_each_productive_child(nodes[frame.node.value].shape,
                                  [&](CoreWireSchemaNodeId child) {
                                      if (!memo[child.value].has_value()) {
                                          stack.push_back({child, false});
                                      }
                                  });
    }
    if (!memo[root.value].has_value()) {
        return std::unexpected(MaxCanonicalSizeError::Unbounded); // fail closed
    }
    return *memo[root.value];
}

} // namespace

std::expected<std::uint64_t, MaxCanonicalSizeError>
max_canonical_json_size(const ir::core::VerifiedWireSchemaBinding &binding) {
    const auto &nodes = binding.table().nodes;
    const auto root = binding.root();
    if (root.value >= nodes.size()) {
        return std::unexpected(MaxCanonicalSizeError::Unbounded); // fail closed
    }
    // Pass 1: whole-graph boundedness + productive-cycle detection, BEFORE any size
    // arithmetic, so Unbounded is reported strictly before SizeOverflow.
    if (const auto err = check_bounded(nodes, root)) {
        return std::unexpected(*err);
    }
    // Pass 2: checked memoized size over the proven-finite DAG.
    return compute_size(nodes, root);
}

} // namespace ahfl::runtime::core_wire_canonical_size
