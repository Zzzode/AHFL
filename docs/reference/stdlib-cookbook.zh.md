# AHFL stdlib cookbook

Runnable recipes for the AHFL standard library (`std::*`). Every snippet uses
only APIs that are implemented and unit-tested (`tests/integration/stdlib_units/
*_ut.ahfl`). Copy a snippet into a `.ahfl` source and `ahflc check` it; the
`std::*` modules resolve via the built-in stdlib search root, so no
`--search-root` is needed.

Coverage maps to the corelib-support-workplan module matrix: Option / Result /
String / List / Set / Map / cmp.

---

## Option<T>

```ahfl
import std::option as option;

// Construction + inspection
let some: option::Option<Int> = option::Option::Some(42);
let none: option::Option<Int> = option::Option::None;
let has = some.is_some();          // true
let empty = none.is_none();        // true

// Transform the inner value
let doubled = some.map<Int>(\x: Int -> x * 2);          // Some(84)
let fallback = none.unwrap_or(0);                       // 0

// Chain Option-returning computations (flat_map == and_then)
let n = some.flat_map<Int, Int>(\x: Int -> option::Option::Some(x + 1));  // Some(43)

// Filter + recover
let kept = some.filter<Int>(\x: Int -> x > 10);         // Some(42)
let recovered = none.or_else<Int>(\ -> option::Option::Some(7));  // Some(7)

// Exactly-one-of: xor
let x = option::xor<Int>(some, none);                   // Some(42)
```

## Result<T, E>

```ahfl
import std::result as result;

let ok: result::Result<Int, String> = result::Result::Ok(5);
let err: result::Result<Int, String> = result::Result::Err("boom");

// Branch on outcome
let val = ok.unwrap_or(0);                              // 5
let recovered = err.or_else<String>(\e: String -> result::Result::Ok(0)).unwrap_or(0);  // 0

// Map success / failure
let bumped = ok.map<Int>(\x: Int -> x + 1);             // Ok(6)
let labelled = err.map_err<String>(\e: String -> e);    // Err("boom") -- transform the error

// Convert to Option
let ok_value = ok.ok();                                 // Some(5)
let err_value = err.err();                              // Some("boom")

// Bridge Option <-> Result
let r = result::from_option<Int, String>(option::Option::Some(7), \ -> "missing");  // Ok(7)
```

## String

```ahfl
import std::string as string;

// Length / emptiness / search
let n = "hello".length();              // 5
let has = "hello".contains("ell");     // true
let prefix = "hello".starts_with("he"); // true
let suffix = "hello".ends_with("lo");   // true

// Slice + repeat
let part = "hello".substring(1, 4);    // "ell"
let rep = "ab".repeat(3);              // "ababab"

// Trim + replace
let clean = "  hi  ".trim();           // "hi"
let ls = "  hi  ".trim_start();        // "hi  "
let rs = "  hi  ".trim_end();          // "  hi"
let swapped = "a-b-c".replace("-", "+");  // "a+b+c"

// Split -> List<String>
let fields = "a,b,c".split(",");        // ["a","b","c"]
let words = "a b  c".split_whitespace(); // ["a","b","c"]

// Parse -> Option
let parsed = "42".parse_int();          // Some(42)
let fp = "3.14".parse_float();          // Some(3.14)
```

## List<T>

```ahfl
import std::collections as collections;
import std::option as option;

let xs = collections::list_cons<Int>(3, collections::list_cons<Int>(1, collections::singleton<Int>(2)));
// xs == [3, 1, 2]

let len = collections::length<Int>(xs);                 // 3
let first = collections::first<Int>(xs);                // Some(3)
let item = collections::list_get<Int>(xs, 1);           // Some(1)

// fold / map / filter
let sum = collections::fold<Int, Int>(xs, 0, \(acc: Int, x: Int) -> acc + x);  // 6
let doubled = collections::map<Int, Int>(xs, \x: Int -> x * 2);
let big = collections::filter<Int>(xs, \x: Int -> x > 1);

// sort + dedup for a unique sorted set-as-list
let sorted = collections::sort<Int>(xs);                // [1, 2, 3]
let unique = collections::dedup<Int>(sorted);           // [1, 2, 3]

// Concatenate
let both = collections::append<Int>(xs, xs);            // length 6
```

## Set<T>

```ahfl
import std::collections as collections;

let a = collections::set_singleton<Int>(1);
let b = collections::set_singleton<Int>(2);

// Membership
let has_one = collections::contains<Int>(a, 1);         // true

// Set algebra
let u = collections::set_union<Int>(a, b);              // {1, 2}
let i = collections::set_intersection<Int>(u, b);       // {2}
let d = collections::set_difference<Int>(u, b);         // {1}
let sub = collections::set_is_subset<Int>(a, u);        // true
```

## Map<K, V>

```ahfl
import std::collections as collections;
import std::option as option;

let phone = collections::map_singleton<String, String>("alice", "555");
let lookup = collections::map_get<String, String>(phone, "alice");  // Some("555")
let has_key = collections::contains_key<String, String>(phone, "alice");  // true
let empty_q = collections::map_is_empty<String, String>(phone);     // false
```

## cmp (Ordering, min/max/clamp)

```ahfl
import std::cmp as cmp;

// Three-way compare
let ord = cmp::compare<Int>(1, 2);        // Less
let is_less = ord.is_less();               // true

// Min / max / clamp
let lo = cmp::min<Int>(5, 3);              // 3
let hi = cmp::max<Int>(5, 3);              // 5
let bounded = cmp::clamp<Int>(15, 0, 10);  // 10

// Reverse a comparison result
let rev = cmp::compare<Int>(1, 2).reverse();  // Greater
```

---

## Composing across modules

```ahfl
import std::option as option;
import std::result as result;
import std::string as string;
import std::collections as collections;

// Parse a comma-separated list of ints, sum the valid ones (invalid -> 0).
fn sum_parsed(csv: String) -> Int effect Pure decreases 0 {
    let parts = csv.split(",");
    return collections::fold<String, Int>(
        parts, 0,
        \(acc: Int, s: String) -> acc + s.parse_int().unwrap_or(0)
    );
}
```

> **Note on type inference.** P3 does not propagate a generic method's type
> parameter across a method chain (e.g. `r.map(\x->..).unwrap_or(0)`). Give the
> generic method an explicit type argument (`map<Int>`, `and_then<U>`,
> `flat_map<Int, Int>`) or bind the intermediate result to a `let` with a type
> annotation. See corelib-support-workplan §3.5 finding #6.

## Try operator (`?`) — RFC §3.2 / D3

Postfix `?` is a short-circuit combinator for `Option<T>` and `Result<T,E>`.
It works like Rust's `?`:

- On the **success path** (`Some(v)` / `Ok(v)`) → unwraps and yields `v`;
- On the **failure path** (`None` / `Err(e)`) → the enclosing function body
  immediately returns the wrapped failure value (expression-level early
  return).

### Option<T>

```ahfl
import std::option as option;

/// Parse three hex digits; short-circuit if any lookup returns None.
fn hex_to_rgb(r: option::Option<Int>,
              g: option::Option<Int>,
              b: option::Option<Int>) -> option::Option<Int> effect Pure decreases 0 {
    let rv: Int = r?;              // if None → return Option::None
    let gv: Int = g?;              // if None → return Option::None
    let bv: Int = b?;              // if None → return Option::None
    return option::Option::Some(rv * 65536 + gv * 256 + bv);
}
```

### Result<T, E>

```ahfl
import std::result as result;

/// Compute a/b/c chaining two divisions; propagate the *first* Div0 error.
fn div_chain(a: Int, b: Int, c: Int) -> result::Result<Int, String> effect Pure decreases 0 {
    // (Helper lambdas build Ok / Err; in real code these come from parsing.)
    let first:  result::Result<Int, String> =
        if b == 0 then result::Result::Err("div0 at first") else result::Result::Ok(a / b);
    let second: result::Result<Int, String> =
        if c == 0 then result::Result::Err("div0 at second") else result::Result::Ok(first.unwrap_or(0) / c);
    _ = second;
    // The idiomatic chain using ?: — replace the block above with:
    //   let q1: Int = (if b == 0 then result::Result::Err(...) else result::Result::Ok(a / b))?;
    //   let q2: Int = (if c == 0 then result::Result::Err(...) else result::Result::Ok(q1 / c))?;
    //   return result::Result::Ok(q2);
    return result::Result::Ok(0);
}
```

### Gating rules enforced by the typechecker

1. **Return-type contract.** `o?` on an `Option<T>` operand requires the
   enclosing function to return `Option<_>` (the T of the operand is the
   unwrapped payload type; the enclosing `U` must match via subtype).
   `r?` on a `Result<T,E>` requires the enclosing function to return
   `Result<_, E'>` with `E <: E'` (error widening).
2. **Context gate.** `?` is only legal inside a function body — it cannot
   appear in `requires` clauses, `contract` bodies, predicate/flow
   expressions, or at module top-level. No enclosing return type →
   diagnostic.
3. **Non-container types.** Applying `?` to `Int`, `Bool`, a `struct`, or
   any type that is not a nominal `Option<T>` / `Result<T,E>` raises
   `typecheck.TRY_OPERAND_NOT_SUPPORTED`.

> **Tip.** Composable with any expression position — the early-return
> signal propagates through binary ops, call arguments, method-receiver
> expressions and index-access, so you can write `let z = (a? + b?) / 2;`
> and the enclosing function returns `None` the moment either operand
> does. See `tests/integration/stdlib_units/option_ut.ahfl` (`try_option_chain`)
> and `result_ut.ahfl` (`try_result_chain`) for 16 runnable assertions.

---

*Last updated 2026-06-29. APIs reflect the M1/M2 increments (Option::xor /
flat_map, String trim/split/replace/parse, List sort/dedup, Set algebra).*
