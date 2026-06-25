export const meta = {
  name: 'p3-blockers-plus-skeletons-v2',
  description: '并行 4 路：impl_fn_item @builtin、InstanceBuildKind 修正、normalize_type_key primitive 分支、创建 5 个 std 缺失模块骨架',
  phases: [
    { title: 'Blocker 1: impl_fn_item @builtin support' },
    { title: 'Blocker 2: InstanceBuildKind mapping' },
    { title: 'Blocker 3: normalize_type_key primitives' },
    { title: 'Create std missing modules' },
    { title: 'Synthesize results' },
  ],
};

phase('Blocker 1: impl_fn_item @builtin support');
const b1 = agent('Implement Blocker 1 fix: add @builtin attribute support to impl_fn_item in AHFL compiler.

Repo root: /Users/bytedance/Develop/AHFL, main checkout only.

Files to modify (do ALL edits carefully, then build, then run targeted tests):

1) grammar/AHFL.g4 — implFnItem rule
   Current lines around 332-334:
     implFnItem:
         \x27fn\x27 identifier typeParams? \x27(\x27 paramList? \x27)\x27 (\x27->\x27 type_)? effectClause?
         whereClause? fnBody;
   Prepend builtinAttr? before \x27fn\x27, matching fnDecl pattern. fnDecl rule around line 233 has: DOC_COMMENT? builtinAttr? \x27fn\x27 ...
   Note: builtinAttr rule is at line 224: builtinAttr: \x27@builtin\x27 \x27(\x27 STRING_LITERAL \x27)\x27;

2) src/compiler/syntax/frontend/ast.cpp — find the code that builds FnDecl for impl_fn (visitImplFnItem or similar). Search for \x22impl_fn_item\x22 or \x22ImplFnItem\x22 or the visitor method attached to that parser rule. When building the FnDecl struct for impl method, extract builtinAttr text (if present) and set fn_decl.builtin_name = attr_string. The same pattern exists for top-level fnDecl visitor — find how it extracts builtin there, and mirror it for impl.

3) src/compiler/semantics/typecheck_decls.cpp or typecheck.cpp — find the code that emits E::builtin_outside_std for non-std modules declaring @builtin fns (search for builtin_outside_std or builtin_name check). Apply the SAME check for impl methods: if a method inside an impl has builtin_name.has_value() AND the containing module canonical name does NOT start with \x22std::\x22, emit E::builtin_outside_std with matching SourceRange.

4) src/compiler/ir/typed_hir_lower.cpp — find function lower_impl_method (around line 2585-2624). If the method FnDecl has builtin_name.has_value(), the lowering should treat it like a builtin. Find the existing pattern for top-level @builtin fns — they probably are marked so calls become BuiltinCallExpr — and mirror that for impl methods. If method has a builtin_name, set a field/marker that when method_candidates picks this method, TypedCallTargetKind becomes Builtin. Simplest approach: when building the impl method\x27s type info (FnTypeInfo, which goes into TypedProgram.fns bucket), copy builtin_name into it (same field used by top-level @builtin). Then visit_method_call lowering already knows how to dispatch based on builtin_name. DO NOT invent a new TypedCallTargetKind value — static_assert enforces count==3. Instead route via the existing Builtin kind by making the resolved_symbol refer to a builtin, OR set a side-channel marker on resolved_symbol that visit_method_call checks. The LESS invasive option: inside visit_method_call (around 1310-1325), AFTER resolving which method is called, check the target impl method\x27s FnDecl.builtin_name via find_fn_info or similar, and if set, emit a BuiltinCallExpr using that name and [receiver + remaining args] instead of a regular CallExpr. Look for existing visit_fn_call handling of Builtin target for exact pattern.

After edits:
  cd /Users/bytedance/Develop/AHFL
  cmake --build --preset build-dev -j 8 2>&1 | tail -30
If build fails due to grammar (antlr regenerate), cmake auto-trigger should work; if not, do: cmake --preset dev first to reconfigure, then build again.

Then run targeted tests:
  ctest --preset test-dev --output-on-failure -L \x27syntax|frontend|grammar\x27 2>&1 | tail -20

Report: each file + line range changed, summary of change, build result, test result.
', {label: 'b1-impl-builtin', phase: 'Blocker 1: impl_fn_item @builtin support', effort: 'high'});

phase('Blocker 2: InstanceBuildKind mapping');
const b2 = agent('Implement Blocker 2 fix: correct InstanceBuildKind mapping for method_call_sites.

Repo root: /Users/bytedance/Develop/AHFL. Main checkout.

File: src/compiler/ir/typed_hir_lower.cpp
Find the loop that processes method_call_sites inside emit_instantiated_declarations (around lines 2680-2720). It currently iterates typed_program_->method_call_sites.

Current buggy code (verify exact lines first, then fix):
  for (const auto &expr : typed_program_->method_call_sites) {
    ... build info.kind based on call_target_kind ...
    if (InherentMethod) { kind=Capability; info.capability=...; }
    else { kind=Predicate; info.predicate=...; }
  }

FIX:
1. For each method_call_site, first look up the Symbol via find_fn_info(expr.resolved_symbol). Since impl methods are registered in TypedProgram.fns by lower_impl_method, find_fn_info should find them when the symbol is valid.
   - If find_fn_info returns non-null AND it has a body (body_block_index or similar valid): set kind = InstanceBuildKind::Fn; info.fn = that fn_info.
   - ELSE, fall back to the Capability / Predicate split based on call_target_kind (the current path). This keeps user capability-method dispatch working.

2. ADD the stdlib-skip guard mirroring line ~2732 used by fn_call_sites: if the resolved symbol\x27s canonical_name starts with \x22std::\x22, skip (stdlib uses RuntimeFunctionTable, not IR InstanceDecl). Find the pattern: check typed_program_->find_symbol(site.fn_symbol) -> get().canonical_name. Do the same for method_call_sites.

After edit:
  cmake --build --preset build-dev -j 8 2>&1 | tail -20
Then:
  ctest --preset test-dev --output-on-failure -L \x27trait|impl|typed_hir|semantics\x27 2>&1 | tail -40

Report file + line numbers changed, build result, test results.
', {label: 'b2-instancebuildkind', phase: 'Blocker 2: InstanceBuildKind mapping', effort: 'high'});

phase('Blocker 3: normalize_type_key primitives');
const b3 = agent('Implement Blocker 3 fix: explicit primitive branches in TypeEnvironment::normalize_type_key.

Repo root: /Users/bytedance/Develop/AHFL. Main checkout.

File: src/compiler/semantics/type_environment.cpp, function TypeEnvironment::normalize_type_key (around lines 309-331).

Currently:
  return type.visit(types::Overloads{
      [](const types::StructT &s) {...},
      [](const types::EnumT &e) {...},
      [](const types::EnumVariantT &v) {...},
      [](const types::FnT &f) {...},
      [](const types::TypeVarT &v) {...},
      [&type](const auto &) { return type.describe(); },
  });

The catch-all branch returns describe() which gives \x22String\x22, \x22Int\x22, etc. for primitives. Add EXPLICIT handlers for each primitive BEFORE the catch-all:

- StringT: return \x22primitive:String\x22
- BoundedStringT: return \x22primitive:BoundedString:\x22 + std::to_string(value.length_bound) + \x22:\x22 + (value.refinement ? value.refinement->describe() : std::string{}\x22)
  (or at minimum length_bound — do what you can with the fields present; inspect BoundedStringT struct first for exact fields)
- UUIDT: return \x22primitive:UUID\x22
- TimestampT: return \x22primitive:Timestamp\x22
- DurationT: return \x22primitive:Duration\x22
- IntT: return \x22primitive:Int\x22 (check if IntT exists as a tag struct in types.hpp — it may be named Int1T, Int32T, etc. or a single IntT; look first)
- FloatT: return \x22primitive:Float\x22  (same caveat)
- BoolT: return \x22primitive:Bool\x22
- UnitT: return \x22primitive:Unit\x22  (or VoidT — check names in types.hpp)
- AnyT / NeverT / ErrorT / TypeVarT already explicit or last — leave as-is

Do NOT include ListT/SetT/MapT/OptionalT — those are forbidden-sugar forward decls per P5.4/R-01 and have no payload fields.

After editing:
  cmake --build --preset build-dev -j 8 2>&1 | tail -20
Then:
  ctest --preset test-dev --output-on-failure -L \x27type_environment|type_relations|trait|impl\x27 2>&1 | tail -30

Report changes + build + test result.
', {label: 'b3-normalize-key', phase: 'Blocker 3: normalize_type_key primitives', effort: 'medium'});

phase('Create std missing modules');
const skel = agent('Create 5 MISSING std module .ahfl skeleton files.

Repo root: /Users/bytedance/Develop/AHFL. Main checkout only.

First, inspect what exists:
- List std/*.ahfl, check content of result.ahfl and prelude.ahfl and time.ahfl and uuid.ahfl to understand existing patterns (module headers, @builtin usage, effect Pure syntax, decreases, match syntax, etc.)

Create the following new files (only create if they DO NOT exist; if exists skip or merge):

A) std/cmp.ahfl — per spec §6.1 (Ordering enum):
```ahfl
module std::cmp;

enum Ordering {
    Less,
    Equal,
    Greater,
}
```

B) std/traits.ahfl — per spec §6.1 (Eq/Ord/Hash) + §6.2 (Iterable/Foldable/Functor) + §6.3 (Display/Debug) + FromStr (from string spec §7.1).
WARNING: Do NOT include FromJson/ToJson here — those go in std/json.ahfl because they depend on JsonValue.
Content:
```ahfl
module std::traits;

import std::cmp as cmp;
import std::collections;
import std::option as option;

// ———————————————————— §6.1 相等/序/哈希 ————————————————————
trait Eq {
    fn eq(self, other: Self) effect Pure -> Bool;
    fn ne(self, other: Self) effect Pure -> Bool {
        return not eq(self, other);
    }
}

trait Ord: Eq {
    fn compare(self, other: Self) effect Pure -> cmp::Ordering;
    fn lt(self, other: Self) effect Pure -> Bool {
        return compare(self, other) == cmp::Ordering::Less;
    }
    fn le(self, other: Self) effect Pure -> Bool {
        return compare(self, other) != cmp::Ordering::Greater;
    }
    fn gt(self, other: Self) effect Pure -> Bool {
        return compare(self, other) == cmp::Ordering::Greater;
    }
    fn ge(self, other: Self) effect Pure -> Bool {
        return compare(self, other) != cmp::Ordering::Less;
    }
    fn max(self, other: Self) effect Pure -> Self {
        if gt(self, other) { return self; }
        return other;
    }
    fn min(self, other: Self) effect Pure -> Self {
        if lt(self, other) { return self; }
        return other;
    }
}

trait Hash {
    fn hash(self) effect Pure -> Int;
}

// ———————————————————— §6.2 容器抽象 ————————————————————
trait Iterable<T> {
    fn next(self) effect Pure -> option::Option<(Self, T)>;
}

trait Foldable<T> {
    fn length(self) effect Pure -> Int;
    fn fold<A>(self, init: A, f: Fn(A, T) -> A) effect Pure decreases length(self) -> A;
}

trait Functor<T> {
    fn map<U>(self, f: Fn(T) -> U) effect Pure -> Self;
}

// ———————————————————— §6.3 格式化/调试 ————————————————————
trait Display {
    fn display(self) effect Pure -> String;
}

trait Debug {
    fn debug(self) effect Pure -> String;
}

// ———————————————————— FromStr (字符串解析) ————————————————————
trait FromStr {
    fn from_str(s: String) effect Pure -> option::Option<Self>;
}
```

C) std/fmt.ahfl — per spec §7.2 (skeleton; full format impl is complex)
```ahfl
module std::fmt;

import std::collections;
import std::traits;

// NOTE: DisplayValue is an opaque boxed (value + display fn) wrapper.
// Implementation uses structural type; users call display<T: Display>(x).
struct DisplayValue {
    rendered: String,
}

fn display<T: traits::Display>(x: T) effect Pure -> DisplayValue decreases 0 {
    return DisplayValue { rendered: x.display() };
}

fn fmt_raw_concat(a: String, b: String) -> String effect Pure decreases 0 {
    // bridge to builtin concat in std::string; if direct call not possible, use a workaround.
    return a;
}

// Minimal placeholder: join DisplayValue into a single string.
// Actual format! templating engine deferred to P6 follow-up.
fn format_placeholder(args: collections::List<DisplayValue>) effect Pure -> String decreases collections::length<DisplayValue>(args) {
    return \x22\x22;
}
```
Note: This fmt skeleton is deliberately minimal. A full templating engine implementation with {} / {n} / {name} placeholder parsing requires a parser; implementing that is not part of the skeleton scope. The skeleton is valid but not fully functional yet.

D) std/decimal.ahfl — per spec §7.3
```ahfl
module std::decimal;

import std::cmp as cmp;
import std::traits;
import std::option as option;

enum RoundingMode {
    HalfUp,
    HalfDown,
    HalfEven,
    Truncate,
    Ceiling,
    Floor,
}

// NOTE: Decimal is a native compiler primitive type; we do NOT re-declare it here.
// The functions below operate on the native Decimal.

@builtin(\x22decimal_raw_add\x22)
fn raw_add(a: Decimal, b: Decimal, scale: Int) -> Decimal effect Pure;
@builtin(\x22decimal_raw_mul\x22)
fn raw_mul(a: Decimal, b: Decimal, scale: Int) -> Decimal effect Pure;

fn from_int(n: Int) -> Decimal effect Pure decreases 0 {
    return raw_add(from_int_0(n), from_int_0(0), 0);
}
fn from_int_0(n: Int) -> Decimal effect Pure decreases 0 {
    // Placeholder — @builtin bridge
    return raw_add(make_zero(), make_zero(), 0);
}
fn make_zero() -> Decimal effect Pure decreases 0 {
    return raw_add(raw_zero(), raw_zero(), 0);
}
@builtin(\x22decimal_zero\x22)
fn raw_zero() -> Decimal effect Pure;

fn scale_of(a: Decimal) -> Int effect Pure decreases 0 { return 0; }
fn compare(a: Decimal, b: Decimal) -> cmp::Ordering effect Pure decreases 0 { return cmp::Ordering::Equal; }
```
Note: deliberately minimal — the detailed semantics match the spec; full arithmetic uses the 2 @builtin hooks per §5.3.

E) std/json.ahfl — per spec §10.3 (ADTs + traits + accessors)
```ahfl
module std::json;

import std::option as option;
import std::collections;
import std::traits;

enum JsonNumber {
    Int(Int),
    Float(Float),
}

enum JsonValue {
    Null,
    Bool(Bool),
    Number(JsonNumber),
    String(String),
    Array(collections::List<JsonValue>),
    Object(collections::Map<String, JsonValue>),
}

// ———————————————————— parse / emit ————————————————————
@builtin(\x22json_parse_raw\x22)
fn json_parse_raw(s: String) -> option::Option<JsonValue> effect Pure;
@builtin(\x22json_emit_raw\x22)
fn json_emit_raw(v: JsonValue) -> String effect Pure;

fn parse(s: String) effect Pure -> option::Option<JsonValue> decreases 0 { return json_parse_raw(s); }
fn emit(v: JsonValue) effect Pure -> String decreases 0 { return json_emit_raw(v); }

// ———————————————————— predicates ————————————————————
fn is_null(v: JsonValue) -> Bool effect Pure decreases 0 {
    return match v { Null => true, _ => false };
}
fn is_bool(v: JsonValue) -> Bool effect Pure decreases 0 {
    return match v { Bool(_) => true, _ => false };
}
fn is_number(v: JsonValue) -> Bool effect Pure decreases 0 {
    return match v { Number(_) => true, _ => false };
}
fn is_string(v: JsonValue) -> Bool effect Pure decreases 0 {
    return match v { String(_) => true, _ => false };
}
fn is_array(v: JsonValue) -> Bool effect Pure decreases 0 {
    return match v { Array(_) => true, _ => false };
}
fn is_object(v: JsonValue) -> Bool effect Pure decreases 0 {
    return match v { Object(_) => true, _ => false };
}

// ———————————————————— accessors (body minimal skeleton) ————————————————————
fn as_bool(v: JsonValue) -> option::Option<Bool> effect Pure decreases 0 {
    return match v { Bool(b) => option::Option::Some(b), _ => option::Option::None };
}
fn as_int(v: JsonValue) -> option::Option<Int> effect Pure decreases 0 {
    return match v {
        Number(JsonNumber::Int(n)) => option::Option::Some(n),
        _ => option::Option::None,
    };
}
fn as_float(v: JsonValue) -> option::Option<Float> effect Pure decreases 0 {
    return match v {
        Number(JsonNumber::Float(x)) => option::Option::Some(x),
        _ => option::Option::None,
    };
}
fn as_string(v: JsonValue) -> option::Option<String> effect Pure decreases 0 {
    return match v { String(s) => option::Option::Some(s), _ => option::Option::None };
}
fn as_array(v: JsonValue) -> option::Option<collections::List<JsonValue>> effect Pure decreases 0 {
    return match v { Array(xs) => option::Option::Some(xs), _ => option::Option::None };
}
fn as_object(v: JsonValue) -> option::Option<collections::Map<String, JsonValue>> effect Pure decreases 0 {
    return match v { Object(m) => option::Option::Some(m), _ => option::Option::None };
}

// ———————————————————— cross-module traits (§6.3) ————————————————————
trait FromJson {
    fn from_json(v: JsonValue) effect Pure -> option::Option<Self>;
}
trait ToJson {
    fn to_json(self) effect Pure -> JsonValue;
}
```

AFTER creating all 5 files:
1. Build C++ side (should be unaffected by std/*.ahfl data files):
   cmake --build --preset build-dev -j 8 2>&1 | tail -10
2. Find the ahflc binary: find /Users/bytedance/Develop/AHFL/build/dev -name ahflc -type f -perm +111 2>/dev/null | head -3
3. For each new file try:  $AHFLC check --search-root /Users/bytedance/Develop/AHFL/std --module-name std::cmp 2>&1 (substitute the correct module name). Report results.
4. If parse failures occur due to grammar mismatch (e.g. trait Ord: Eq syntax, where Self not yet supported, etc.), note specific errors and adjust files accordingly until they parse.

Report exactly what was created and the parse/check results for each.
', {label: 'skel-std-modules', phase: 'Create std missing modules', effort: 'high'});

phase('Synthesize results');
const all = await parallel([() => b1, () => b2, () => b3, () => skel]);
log('All 4 tasks done; build+parse checks aggregated.');
return {b1: all[0], b2: all[1], b3: all[2], skel: all[3]};
