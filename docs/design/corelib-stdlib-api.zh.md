# AHFL Corelib stdlib 完整接口规范

本文是 [corelib RFC](../rfcs/0013-corelib-type-system-evolution.zh.md) §3.3 stdlib 各模块的**详细接口规范**附件，把 RFC 表格中的模块清单展开为可直接据以实现的完整签名。同时决议主 RFC §7 的开放问题 4（`std::result` 与现有错误模型）与开放问题 5（prelude stability policy）。

- 母文档：[RFC 0013](../rfcs/0013-corelib-type-system-evolution.zh.md)（设计哲学、类型系统四大支柱、可验证子集、迁移路径 P0–P7）
- 语言规范：[core-language.zh.md](../spec/core-language.zh.md)（§4.6.8 ExprEffect 6 级、§3.4 capability、§4.6.5 capability 调用规则）
- 形式化后端：[formal-backend.zh.md](./formal-backend.zh.md)（SMV 编码、observation 抽象）
- 模块解析：[module-resolution-rules.zh.md](./module-resolution-rules.zh.md)

风格与母文档严格一致：中文正文、技术对照用表格、EBNF 用 ```` ```ebnf ```` 代码块、ahfl 示例用 ```` ```ahfl ````、架构/流程用 mermaid、诊断码用 `E::xxx` 形式。本文不落代码，只产出接口规范与决议；模块实现见 RFC P6。

---

## 1. 设计约束与术语

### 1.1 类型系统前置

本文所有签名假设主 RFC §3.2 的类型系统四大支柱已落地：

1. **ADT**（§3.2.1）：`enum` 带 payload + `match` 模式匹配 + exhaustiveness
2. **用户泛型 + trait**（§3.2.2）：`fn`/`impl`/`trait`/orphan 严格版 coherence
3. **一等闭包**（§3.2.3）：`Fn(A,T)->A` 是一等类型，闭包可传递/返回/存储
4. **统一 effect 系统**（§3.2.4）：`effect Pure | Nondet | <Capability>+`，`decreases <度量>` 终止度量

签名中出现的语法元素含义：

| 元素 | 含义 |
| --- | --- |
| `effect Pure` | 纯、确定、终止、无副作用；可进可验证子集 |
| `effect Pure decreases E` | 纯且编译器证终止，度量 `E` 严格下降 |
| `effect Nondet` | 非确定（读 wall-clock / 随机源） |
| `effect IO` / `effect <Capability>` | 调用 capability（effectful），不进子集 |
| `@builtin("name")` | 编译器原语 hook，仅 `std` 模块可调用 |
| `where length <= N` | bounded refinement，SMV 编码前置 |
| `impl<T> Trait<T> for Type<T>` | trait 实现，受 orphan rule 约束 |

### 1.2 effect 与现有 ExprEffect 6 级的关系

主 RFC §3.2.4 已确立：现有 `ExprEffect`（Pure / ConstOnly / PredicateCall / CapabilityCall / ExternalEffect / Unknown，见 `effects.cpp` 与 spec §4.6.8）成为统一 effect 系统在**表达式层面的推导结果**，而非并行机制。本文签名中的 `effect` 子句对应 stdlib 函数自身的声明级 effect，调用点的 `ExprEffect` 由其推导：

| stdlib 函数 effect 子句 | 调用点的 `ExprEffect`（推导结果） | 是否纯（rank ≤ 2） |
| --- | --- | --- |
| `effect Pure` | `Pure` 或 `PredicateCall`（视是否含输入） | 是 |
| `effect Pure decreases E` | `Pure` / `PredicateCall` | 是 |
| `effect Nondet` | `ExternalEffect`（非确定环境输入） | 否 |
| `effect <Capability>+` | `CapabilityCall` 或 `ExternalEffect`（按 effect profile） | 否 |

因此集合方法 `values.fold(...)`（`effect Pure`，递减度量来自集合长度）调用点推导为 `PredicateCall`，可自由进入 `requires` / `ensures` / `if` / `assert`；而 `now`（`effect Nondet`）调用点推导为 `ExternalEffect`，禁止进入纯表达式位置，违反报 `E::effect_not_pure`。

### 1.3 `@builtin` hook 的约束

`@builtin("name")` 是 stdlib 访问真正原语的**唯一入口**，约束与 RFC §3.3 一致：

1. 数量冻结，新增必须经 RFC（完整清单见 §5）
2. 单向出边：只有 `std::*` 模块可声明/调用 `@builtin`；用户模块直接调用报 `E::builtin_outside_std`
3. `@builtin` 函数自身的 effect 由其语义决定，必须显式标注（不允许省略）
4. `@builtin` 不进入可验证子集；调用 `@builtin` 的 stdlib 包装函数若想进子集，必须把非纯 `@builtin` 排除在 `effect Pure` 体内

---

## 2. std::option — `Option<T>` ADT

对标 Rust `core::option::Option`、Swift `Optional`、Haskell `Maybe`。

### 2.1 类型定义

```ahfl
module std::option;

enum Option<T> {
    Some(T),
    None,
}
```

构造子受现有文法 `some(e)` / `none` 字面量语法糖覆盖（见 RFC P5：`some(e)` desugar 为 `Option::Some(e)`，`none` 在期望类型为 `Option<T>` 时 desugar 为 `Option::None`）。模式匹配：

```ahfl
match opt {
    Some(x) => ...,
    None    => ...,
}
```

### 2.2 当前接口

接收者操作使用 inherent method，例如 `value.map(f)` 或 `value.length()`；
不导出重复的模块级接收者 wrapper。构造与独立算法保留显式 free function。
以下签名与当前 std 实现一致；函数体以源码为准。

```ahfl
impl<T> Option<T> {
    pub fn is_some(self) -> Bool effect Pure decreases 0;
    pub fn is_none(self) -> Bool effect Pure decreases 0;
    pub fn map<U>(self, f: Fn(T) -> U) -> Option<U> effect Pure decreases 0;
    pub fn and_then<U>(self, f: Fn(T) -> Option<U>) -> Option<U> effect Pure decreases 0;
    pub fn or_else(self, alt: Fn() -> Option<T>) -> Option<T> effect Pure decreases 0;
    pub fn filter(self, p: Fn(T) -> Bool) -> Option<T> effect Pure decreases 0;
    pub fn unwrap_or(self, default: T) -> T effect Pure decreases 0;
    pub fn unwrap_or_else(self, default: Fn() -> T) -> T effect Pure decreases 0;
    pub fn get_or_insert(self, value: T) -> T effect Pure decreases 0;
    pub fn xor(self, other: Option<T>) -> Option<T> effect Pure decreases 0;
}

pub fn xor<T>(self: Option<T>, other: Option<T>) -> Option<T> effect Pure decreases 0;
pub fn flat_map<T, U>(self: Option<T>, f: Fn(T) -> Option<U>) -> Option<U> effect Pure decreases 0;
pub fn filter_value<T>(value: T, p: Fn(T) -> Bool) -> Option<T> effect Pure decreases 0;
pub fn some<T>(value: T) -> Option<T> effect Pure decreases 0;
pub fn option_none<T>() -> Option<T> effect Pure decreases 0;
```

### 2.3 构造与导入

`Option<T>` 是 nominal enum；使用 `Option::Some(value)` / `Option::None` 和正常
模式匹配。prelude 显式导出 Option；导入别名遵守普通名称解析规则，不选择历史
类型实现，也不承诺保留任何过渡期 API。

---

## 3. std::result — `Result<T,E>` ADT

对标 Rust `core::result::Result`、Swift `Result`、Haskell `Either`。

### 3.1 类型定义

```ahfl
module std::result;

enum Result<T, E> {
    Ok(T),
    Err(E),
}
```

### 3.2 当前接口

接收者操作使用 inherent method，例如 `value.map(f)` 或 `value.length()`；
不导出重复的模块级接收者 wrapper。构造与独立算法保留显式 free function。
以下签名与当前 std 实现一致；函数体以源码为准。

```ahfl
impl<T, E> Result<T, E> {
    pub fn is_ok(self) -> Bool effect Pure decreases 0;
    pub fn is_err(self) -> Bool effect Pure decreases 0;
    pub fn map<U>(self, f: Fn(T) -> U) -> Result<U, E> effect Pure decreases 0;
    pub fn map_err<F>(self, f: Fn(E) -> F) -> Result<T, F> effect Pure decreases 0;
    pub fn and_then<U>(self, f: Fn(T) -> Result<U, E>) -> Result<U, E> effect Pure decreases 0;
    pub fn or_else<F>(self, alt: Fn(E) -> Result<T, F>) -> Result<T, F> effect Pure decreases 0;
    pub fn unwrap_or(self, default: T) -> T effect Pure decreases 0;
    pub fn unwrap_or_else(self, default: Fn(E) -> T) -> T effect Pure decreases 0;
    pub fn ok(self) -> option::Option<T> effect Pure decreases 0;
    pub fn err(self) -> option::Option<E> effect Pure decreases 0;
}

pub fn from_option<T, E>(opt: option::Option<T>, mk_err: Fn() -> E) -> Result<T, E> effect Pure decreases 0;
pub fn flat_map<T, U, E>(self: Result<T, E>, f: Fn(T) -> Result<U, E>) -> Result<U, E> effect Pure decreases 0;
pub fn flatten<T, E>(self: Result<Result<T, E>, E>) -> Result<T, E> effect Pure decreases 0;
pub fn transpose<T, E>(self: Result<option::Option<T>, E>) -> option::Option<Result<T, E>> effect Pure decreases 0;
```

### 3.3 `try` / `?` 操作符

`?` 操作符是语法糖，**仅**在返回类型为 `Result<T, E>` 或 `Option<T>` 的 `fn` 体内合法。`expr?` desugar 规则：

```text
expr : Result<T, E>   in fn returning Result<R, E>
------------------------------------------------------
match expr {
    Ok(v)  => v,
    Err(e) => return Err(e),
}

expr : Option<T>      in fn returning Option<R>
------------------------------------------------------
match expr {
    Some(v) => v,
    None    => return None,
}
```

`?` 在 capability `flow` handler 中**不允许**——`flow` 的失败由 capability fail-closed 模型承载（见 §8 决议 4）。`?` 在 predicate / contract 表达式中**不允许**——这些位置要求纯表达式，`?` 携带隐式 `return` 属于控制流副作用。

---

## 4. std::collections — `List/Set/Map` + 高阶 trait

对标 Rust `std::collections` + `Iterator`/`FromIterator`、Swift `Array/Set/Dictionary`、Haskell `Foldable`/`Functor`。

### 4.1 类型定义

```ahfl
module std::collections;

// 三个容器底层由 @builtin 暴露的原语数组实现 (RFC P5: 结构性 op 下沉为 @builtin)
// 用户看到的形态是带 bounded refinement 的泛型类型
type List<T> where length <= MAX_LIST_LEN;       // 默认 65536, 用户可显式收窄
type Set<T: Hash>;
type Map<K: Hash, V>;
```

字面量语法糖（保留 RFC P5 的语法友好性）：

```ahfl
[1, 2, 3]                       // List<Int>
set[1, 2, 3]                    // Set<Int>
map["a": 1, "b": 2]             // Map<String, Int>
```

### 4.2 当前接口（List）

接收者操作使用 inherent method，例如 `value.map(f)` 或 `value.length()`；
不导出重复的模块级接收者 wrapper。构造与独立算法保留显式 free function。
以下签名与当前 std 实现一致；函数体以源码为准。

```ahfl
impl<T> List<T> {
    pub fn is_empty(self) -> Bool effect Pure decreases 0;
    pub fn length(self) -> Int effect Pure decreases 0;
    pub fn get(self, i: Int) -> option::Option<T> effect Pure decreases 0;
    pub fn first(self) -> option::Option<T> effect Pure decreases 0;
    pub fn last(self) -> option::Option<T> effect Pure decreases 0;
    pub fn append(self, other: List<T>) -> List<T> effect Pure decreases list_raw_length<T>(self) + list_raw_length<T>(other);
    pub fn map<U>(self, f: Fn(T) -> U) -> List<U> effect Pure decreases list_raw_length<T>(self);
    pub fn filter(self, p: Fn(T) -> Bool) -> List<T> effect Pure decreases list_raw_length<T>(self);
    pub fn fold<U>(self, init: U, f: Fn(U, T) -> U) -> U effect Pure decreases list_raw_length<T>(self);
    pub fn contains(self, x: T) -> Bool effect Pure decreases list_raw_length<T>(self);
    pub fn tail(self) -> option::Option<List<T>> effect Pure decreases 0;
    pub fn concat(self, others: List<List<T>>) -> List<T> effect Pure decreases list_raw_length<List<T>>(others);
    pub fn slice(self, start: Int, end: Int) -> option::Option<List<T>> effect Pure decreases 0;
    pub fn insert(self, idx: Int, x: T) -> option::Option<List<T>> effect Pure decreases 0;
    pub fn remove(self, x: T) -> List<T> effect Pure decreases list_raw_length<T>(self);
    pub fn remove_at(self, idx: Int) -> option::Option<List<T>> effect Pure decreases 0;
    pub fn fold_right<U>(self, init: U, f: Fn(T, U) -> U) -> U effect Pure decreases list_raw_length<T>(self);
    pub fn flat_map<U>(self, f: Fn(T) -> List<U>) -> List<U> effect Pure decreases list_raw_length<T>(self);
    pub fn reverse(self) -> List<T> effect Pure decreases list_raw_length<T>(self);
}

pub fn list_cons<T>(x: T, xs: List<T>) -> List<T> effect Pure decreases 0;
pub fn empty<T>() -> List<T> effect Pure decreases 0;
pub fn singleton<T>(x: T) -> List<T> effect Pure decreases 0;
pub fn sort<T>(self: List<T>) -> List<T> effect Pure decreases 0;
pub fn dedup<T>(self: List<T>) -> List<T> effect Pure decreases 0;
```

### 4.3 当前接口（Set）

接收者操作使用 inherent method，例如 `value.map(f)` 或 `value.length()`；
不导出重复的模块级接收者 wrapper。构造与独立算法保留显式 free function。
以下签名与当前 std 实现一致；函数体以源码为准。

```ahfl
impl<T> Set<T> {
    pub fn is_empty(self) -> Bool effect Pure decreases 0;
    pub fn length(self) -> Int effect Pure decreases 0;
    pub fn contains(self, x: T) -> Bool effect Pure decreases 0;
}

pub fn set_empty<T>() -> Set<T> effect Pure decreases 0;
pub fn set_singleton<T>(x: T) -> Set<T> effect Pure decreases 0;
```

### 4.4 当前接口（Map）

接收者操作使用 inherent method，例如 `value.map(f)` 或 `value.length()`；
不导出重复的模块级接收者 wrapper。构造与独立算法保留显式 free function。
以下签名与当前 std 实现一致；函数体以源码为准。

```ahfl
impl<K, V> Map<K, V> {
    pub fn is_empty(self) -> Bool effect Pure decreases 0;
    pub fn length(self) -> Int effect Pure decreases 0;
    pub fn contains_key(self, key: K) -> Bool effect Pure decreases 0;
    pub fn get(self, key: K) -> option::Option<V> effect Pure decreases 0;
    pub fn get_or(self, key: K, default: V) -> V effect Pure decreases 0;
}

pub fn map_empty<K, V>() -> Map<K, V> effect Pure decreases 0;
pub fn map_singleton<K, V>(key: K, value: V) -> Map<K, V> effect Pure decreases 0;
pub fn map_values<K, V, U>(self: Map<K, V>, f: Fn(V) -> U) -> Map<K, U> effect Pure decreases map_raw_size<K, V>(self);
pub fn filter_keys<K, V>(self: Map<K, V>, p: Fn(K) -> Bool) -> Map<K, V> effect Pure decreases map_raw_size<K, V>(self);
```

### 4.5 SMV 编码对应

容器 SMV 编码策略与 RFC §5 表格一致：

| stdlib 类型 | SMV 编码 | 前置 refinement | 编码位置 |
| --- | --- | --- | --- |
| `List<T> where length <= N` | fixed-size array（N 槽 + valid bit + length 计数器） | `length <= N` | emit-smv bounded array |
| `Set<T: Hash>` over finite `T` | bit-vector over finite domain | `T` 有限域 | emit-smv bitset |
| `Map<K: Hash, V>` over finite `K` | array over finite `K` | `K` 有限域 | emit-smv array |
| `Option<T>` | `T` + valid bit | `T` 可编码 | emit-smv option |
| `Result<T,E>` | `(T, E, is_ok)` 三元组 | `T`/`E` 可编码 | emit-smv tagged union |

进可验证子集的容器操作（`fold`/`map`/`filter`）必须是 `effect Pure decreases length(self)`，使编译器能在 SMV 中展开为有限 bounded 循环；`sort`/`flat_map` 默认不进子集（终止度量复杂，需用户显式 `decreases` 提示）。

---

## 5. `@builtin` hook 完整清单

冻结清单，对标 Rust lang items、Swift `Builtin` module、Zig `@builtin`。新增必须经 RFC，且只能在 `std::*` 模块调用。

### 5.1 容器结构性原语

| hook 签名 | 语义 | 效果 |
| --- | --- | --- |
| `@builtin("list_raw_get") fn list_raw_get<T>(xs: List<T>, i: Int) -> T` | 原始下标读，越界 UB（仅 std 内部用，外层 `get` 包装为 `Option<T>`） | Pure |
| `@builtin("list_raw_set") fn list_raw_set<T>(xs: List<T>, i: Int, x: T) -> List<T>` | 原始下标写 | Pure |
| `@builtin("list_raw_length") fn list_raw_length<T>(xs: List<T>) -> Int` | 原始长度 | Pure |
| `@builtin("list_raw_alloc") fn list_raw_alloc<T>(n: Int) -> List<T>` | 分配 N 槽未初始化 list | Pure |
| `@builtin("set_raw_contains") fn set_raw_contains<T: Hash>(s: Set<T>, x: T) -> Bool` | 哈希集合成员判定 | Pure |
| `@builtin("map_raw_get") fn map_raw_get<K: Hash, V>(m: Map<K,V>, k: K) -> V` | 哈希映射读，缺键 UB | Pure |
| `@builtin("map_raw_contains_key") fn map_raw_contains_key<K: Hash, V>(m: Map<K,V>, k: K) -> Bool` | 哈希映射键存在 | Pure |

调用约束：`list_raw_get`/`map_raw_get` 不做边界检查，由 `std::collections::get` 在其上包一层 `Option<T>` 返回。直接在用户模块调用任一 `@builtin` 报 `E::builtin_outside_std`。

### 5.2 字符串/字节原语

| hook 签名 | 语义 | 效果 |
| --- | --- | --- |
| `@builtin("string_raw_bytes") fn string_raw_bytes(s: String) -> List<Int>` | 取 UTF-8 字节码点列表（bounded） | Pure |
| `@builtin("string_raw_from_bytes") fn string_raw_from_bytes(bs: List<Int>) -> String` | 由码点列表构造字符串 | Pure |
| `@builtin("string_raw_length") fn string_raw_length(s: String) -> Int` | 字符数 | Pure |
| `@builtin("string_raw_slice") fn string_raw_slice(s: String, from: Int, to: Int) -> String` | 子串 | Pure |
| `@builtin("string_raw_concat") fn string_raw_concat(a: String, b: String) -> String` | 拼接 | Pure |

### 5.3 数值/类型原语

| hook 签名 | 语义 | 效果 |
| --- | --- | --- |
| `@builtin("int_to_string") fn int_to_string(n: Int) -> String` | 整数转十进制字符串 | Pure |
| `@builtin("float_to_string") fn float_to_string(x: Float) -> String` | 浮点转字符串 | Pure |
| `@builtin("string_to_int") fn string_to_int(s: String) -> Option<Int>` | 字符串解析为整数 | Pure |
| `@builtin("decimal_raw_add") fn decimal_raw_add(a: Decimal, b: Decimal, scale: Int) -> Decimal` | 固定 scale 十进制加 | Pure |
| `@builtin("decimal_raw_mul") fn decimal_raw_mul(a: Decimal, b: Decimal, scale: Int) -> Decimal` | 固定 scale 十进制乘 | Pure |

### 5.4 时间/非确定原语

| hook 签名 | 语义 | 效果 |
| --- | --- | --- |
| `pub capability Clock() -> Timestamp` | 读 wall-clock 当前时间戳(host capability,RFC 0022 slice 1c;曾为 `@builtin("wall_clock_now")` Nondet) | `effect: read` |
| `@builtin("duration_from_ms") fn duration_from_ms(ms: Int) -> Duration` | 构造 Duration | Pure |
| `@builtin("timestamp_add") fn timestamp_add(t: Timestamp, d: Duration) -> Timestamp` | 时间戳算术 | Pure |

### 5.5 标识/序列化原语

| hook 签名 | 语义 | 效果 |
| --- | --- | --- |
| `pub capability UuidV4() -> UUID` | 生成 v4 UUID(host capability,RFC 0022 slice 1c;曾为 `@builtin("uuid_new")` Nondet) | `effect: read` |
| `@builtin("uuid_from_string") fn uuid_from_string(s: String) -> Option<UUID>` | 解析 UUID 字符串 | Pure |
| `@builtin("json_parse_raw") fn json_parse_raw(s: String) -> Option<JsonValue>` | 原始 JSON 解析（结果类型见 §10） | Pure |
| `@builtin("json_emit_raw") fn json_emit_raw(v: JsonValue) -> String` | 原始 JSON 序列化 | Pure |

### 5.6 调用约束小结

```ahfl
// ✅ 合法：在 std::string 内调用 @builtin
module std::string;
impl String {
    @builtin("string_raw_length")
    pub fn length(self) -> Int effect Pure;
}

// ❌ 非法：用户模块直接调用 @builtin
module app::service;
fn bad(s: String) -> Int {
    @builtin("string_raw_length")(s)    // E::builtin_outside_std
}
```

诊断：`E::builtin_outside_std`（用户模块直接调用 `@builtin`）、`E::builtin_unknown`（hook 名不在冻结清单内）、`E::builtin_missing_effect`（`@builtin` 函数声明省略 effect 标注）。

---

## 6. trait 完整签名

对标 Rust `std::iter`/`std::cmp`/`std::fmt`/`std::hash`、Haskell typeclass、Swift protocol。

### 6.1 `Eq` / `Ord` / `Hash`

```ahfl
trait Eq {
    fn eq(self, other: Self) effect Pure -> Bool;
    fn ne(self, other: Self) effect Pure -> Bool { not eq(self, other) }   // 默认实现
}
// 自反/对称/传递由 contract 承载 (用户写 impl 时可选):
//   requires: forall x. eq(x, x);
//   requires: forall x y. eq(x, y) => eq(y, x);
//   requires: forall x y z. eq(x, y) and eq(y, z) => eq(x, z);

trait Ord: Eq {
    fn compare(self, other: Self) effect Pure -> Ordering;     // Ordering = enum { Less, Equal, Greater }
    fn lt(self, other: Self) effect Pure -> Bool { compare(self, other) == Less }
    fn le(self, other: Self) effect Pure -> Bool { compare(self, other) != Greater }
    fn gt(self, other: Self) effect Pure -> Bool { compare(self, other) == Greater }
    fn ge(self, other: Self) effect Pure -> Bool { compare(self, other) != Less }
    fn max(self, other: Self) effect Pure -> Self { if gt(self, other) { self } else { other } }
    fn min(self, other: Self) effect Pure -> Self { if lt(self, other) { self } else { other } }
    fn clamp(self, lo: Self, hi: Self) effect Pure -> Self;
}

trait Hash {
    fn hash(self) effect Pure -> Int;        // 一致性契约: eq(x,y) => hash(x) == hash(y)
}
```

`Ordering` 在 `std::cmp` 模块定义：`enum Ordering { Less, Equal, Greater }`，进 prelude。

### 6.2 `Foldable` / `Functor` / `Iterable`

```ahfl
// —— 可迭代: 提供 element-by-element 推进 ——
trait Iterable<T> {
    fn next(self) effect Pure -> Option<(Self, T)>;   // 返回 (剩余迭代器, 当前元素), None 表耗尽
    // 注: AHFL 函数式不可变语义下, Iterable 用 (remaining, value) 元组替代 Rust 的 &mut self
}

// —— 可折叠: 提供终止度量 ——
trait Foldable<T> {
    fn length(self) effect Pure -> Int
        ensures result >= 0;
    fn fold<A>(self, init: A, f: Fn(A, T) -> A)
        effect Pure decreases length(self) -> A
        requires effect_of(f) == Pure;
    fn for_each(self, f: Fn(T) -> Unit)
        effect <EffectOf f> decreases length(self) -> Unit;
    fn to_list(self) effect Pure decreases length(self) -> List<T>;
}

// —— 函子: 提供 element-wise 映射 ——
trait Functor<T> {
    fn map<U>(self, f: Fn(T) -> U)
        effect Pure -> Self/*<U>*/
        requires effect_of(f) == Pure;
    // 注: AHFL trait 暂不支持 higher-kinded type (Self<_>),
    //     Functor 通过具体类型而非 HKT 表达, 各容器各自 impl<T,U> map.
    //     此 trait 主要用于约束 effect 签名, 实例化为 List/Set/Map/Option/Result.
}
```

HKT 限制说明：AHFL 当前 trait 系统（RFC P3）不引入 Higher-Kinded Type，避免与 SMV 单态化编码冲突。`Functor<T>` 作为"返回同容器不同元素类型"的契约存在，各容器各自 `impl<T> map<U>`；编译器不强求 `Self<U>` 形态。这与 Rust 当前不提供 `Functor` trait、由各类型各自实现 `map` 的取舍一致。

### 6.3 `Display` / `FromJson` / `ToJson`

```ahfl
trait Display {
    fn display(self) effect Pure -> String;
}

trait Debug {
    fn debug(self) effect Pure -> String;
}

trait FromJson {
    fn from_json(v: JsonValue) effect Pure -> Option<Self>;
    // 失败返回 None; 严格模式用 Result<Self, JsonError> (见 §10.4)
}

trait ToJson {
    fn to_json(self) effect Pure -> JsonValue;
}
```

`Display`/`Debug`/`FromJson`/`ToJson` 全部 `effect Pure`，使其结果可进入纯表达式上下文（`std::fmt::format` 的格式串展开需要 `display` 是纯的）。

### 6.4 默认 trait 实现清单（stdlib 内）

| 类型 | impl 的 trait | 备注 |
| --- | --- | --- |
| `Bool` / `Int` / `Float` / `String` / `Decimal` / `UUID` / `Timestamp` / `Duration` | `Eq`, `Ord`, `Hash`, `Display`, `ToJson` | 全 `effect Pure` |
| `Option<T>` | `Iterable<T>`, `Foldable<T>`, `Eq`/`Ord`/`Hash`/`Display`（依赖内层 T 的对应 trait） | 见 §2.2 |
| `Result<T,E>` | `Iterable<T>`, `Eq`/`Ord`/`Hash`/`Display`（依赖 T、E） | 见 §3.2 |
| `List<T>` | `Iterable<T>`, `Foldable<T>`, `Eq`/`Ord`/`Hash`（依赖 T）, `Display`（依赖 T） | 见 §4.2 |
| `Set<T>` | `Iterable<T>`, `Foldable<T>`, `Eq`/`Hash`（依赖 T） | `Ord` 不实现（无序） |
| `Map<K,V>` | `Iterable<(K,V)>`, `Foldable<(K,V)>`, `Eq`/`Hash`（依赖 K） | `Ord` 不实现 |
| `JsonValue` | `Eq`, `Display`, `ToJson` | 见 §10 |
| 用户 `struct` / `enum` | 由用户写 `impl`；stdlib 不自动 derive（P3 之后通过 `derive(Display,Eq)` 宏扩展） | — |

---

## 7. std::string / std::fmt / std::decimal

### 7.1 std::string

接收者操作使用 inherent method，例如 `value.map(f)` 或 `value.length()`；
不导出重复的模块级接收者 wrapper。构造与独立算法保留显式 free function。
以下签名与当前 std 实现一致；函数体以源码为准。

```ahfl
impl String {
    pub fn is_empty(self) -> Bool effect Pure decreases 0;
    pub fn contains(self, needle: String) -> Bool effect Pure decreases 0;
    pub fn starts_with(self, prefix: String) -> Bool effect Pure decreases 0;
    pub fn ends_with(self, suffix: String) -> Bool effect Pure decreases 0;
    pub fn trim(self) -> String effect Pure decreases 0;
    pub fn trim_start(self) -> String effect Pure decreases 0;
    pub fn trim_end(self) -> String effect Pure decreases 0;
    pub fn replace(self, from: String, to: String) -> String effect Pure decreases 0;
    pub fn split(self, sep: String) -> collections::List<String> effect Pure decreases 0;
    pub fn parse_int(self) -> option::Option<Int> effect Pure decreases 0;
    pub fn split_whitespace(self) -> collections::List<String> effect Pure decreases 0;
    pub fn parse_float(self) -> option::Option<Float> effect Pure decreases 0;
    pub fn concat(self, other: String) -> String effect Pure decreases 0;
    pub fn substring(self, start: Int, end: Int) -> String effect Pure decreases 0;
    pub fn parse_uuid(self) -> option::Option<UUID> effect Pure decreases 0;
    pub fn repeat(self, n: Int) -> String effect Pure decreases 0;
}

pub fn join(strings: collections::List<String>, sep: String) -> String effect Pure decreases collections::list_raw_length<String>(strings);
```

### 7.2 std::fmt

```ahfl
module std::fmt;

// 位置/命名占位符格式化, 对标 Rust format! / Swift String(format:)
fn format(template: String, args: List<DisplayValue>) effect Pure -> String
    requires effect_of_all(args) == Pure;

// DisplayValue 是存在类型封装: (value: T, display_fn: Fn(T) -> String)
type DisplayValue;

// 把任意 Display 包装成 DisplayValue
fn display<T: Display>(x: T) effect Pure -> DisplayValue;

// 模板语法 (冻结, 不支持任意格式指令):
//   {}     位置占位 (依次填入 args)
//   {n}    索引占位
//   {name} 命名占位 (args 中按 name 解析, 仅 FromJson 上下文用)
fn format_to(buf: List<String>, template: String, args: List<DisplayValue>)
    effect Pure -> List<String>;

impl Display for Bool;
impl Display for Int;
impl Display for Float;
impl Display for Decimal;
impl Display for Duration;
impl Display for Timestamp;
impl Display for UUID;
```

### 7.3 std::decimal

```ahfl
module std::decimal;

type Decimal(scale: Int);   // 与 spec §3.2 一致, scale = 小数位数

fn make(n: Int, scale: Int) effect Pure -> Decimal;
fn from_int(n: Int) effect Pure -> Decimal;     // scale = 0
fn add(a: Decimal, b: Decimal) effect Pure -> Decimal
    requires scale_of(a) == scale_of(b);       // 与 spec §4.6.7 一致
fn sub(a: Decimal, b: Decimal) effect Pure -> Decimal
    requires scale_of(a) == scale_of(b);
fn scale_of(a: Decimal) effect Pure -> Int;
fn with_scale(a: Decimal, new_scale: Int) effect Pure -> Decimal;   // 显式 scale 转换
fn compare(a: Decimal, b: Decimal) effect Pure -> Ordering;
fn quantize(a: Decimal, new_scale: Int, mode: RoundingMode) effect Pure -> Decimal;

enum RoundingMode { HalfUp, HalfDown, HalfEven, Truncate, Ceiling, Floor }

impl Eq  for Decimal;
impl Ord for Decimal;
impl Hash for Decimal;
impl Display for Decimal;
```

---

## 8. 开放问题 4：`std::result` 与现有错误模型

### 8.1 现状（错误模型）

AHFL 当前是 **capability fail-closed 错误模型**（spec §3.4 capability、formal-backend §"Call / Effect / Recovery Event 边界"）：

1. capability 是外部 effectful 调用点，自带 effect profile（`read` / `external_side_effect` / `durable_write` / `financial_write` / `unknown`）
2. capability 失败由 runtime / flow handler 的 retry / compensation / recovery lifecycle 承载，不进语言层值通道
3. `flow` handler 的 `with { retry: N; retry_on: [...]; timeout: 30s; }` 是失败策略声明
4. workflow node 有 `AHFL_NODE_FAILED` / `AHFL_NODE_RECOVERING` / `AHFL_NODE_COMPENSATING` / `AHFL_NODE_TERMINAL_FAILED` 等 phase

这套模型由 SMV 的 failure/recovery lifecycle 直接建模（formal-backend 已落地），是 AHFL 可信工作流定位的核心。

### 8.2 决议

`Result<T,E>` **不替代** capability fail-closed 模型，而是**分层共存**。规则如下。

#### 规则 R1：capability 调用结果类型不变，失败仍走 fail-closed

capability 声明的返回类型 `R` 可以是任意值类型（包括 `Result<T,E>`），但 **capability 自身的调用失败（外部 provider 超时、网络错误、effect 失败）不通过 `Result::Err` 编码**，仍由 capability effect profile + retry/compensation/recovery lifecycle 承载。

理由：capability 失败是**效应语义**（effect semantics），不是值语义（value semantics）。SMV 编码已经把失败建模为 phase 迁移（`FAILED`→`RECOVERING`→`RECOVERED`），如果再让失败进值通道，会出现双轨：值层的 `Result::Err` 与 phase 层的 `FAILED` 同时存在且必须同步，违反形式化后端的 observation abstraction 边界。对标 Dafny/F\*：effectful 操作的失败由 effect/trait 承载，不由 ADT 模拟。

#### 规则 R2：`Result<T,E>` 用于值层"业务可恢复失败"

`Result<T,E>` 表达**业务语义层**的、由 capability 返回值显式携带的可恢复失败。例如 `OrderQuery -> Result<OrderInfo, NotFoundError>`，这里 `NotFoundError` 是 provider 正常返回的业务事实（"订单不存在"），不是 provider 自身的崩溃。

| 错误种类 | 承载方式 | 进 SMV |
| --- | --- | --- |
| Provider 崩溃 / 超时 / 网络 | capability effect profile + retry/compensation lifecycle | phase 迁移 |
| Provider 正常返回但业务失败（`NotFound` / `InsufficientFunds`） | `Result<T,E>` 值通道 | tagged union |
| 用户 fn 内部判定的非法状态 | `Result<T,E>` 值通道 | tagged union |
| 完全不可恢复（违反 contract） | `assert false` / runtime panic | 不编码（保证不可达） |

#### 规则 R3：`?` / `try` 仅在 `fn` 内、返回 `Result`/`Option` 的语境下生效

`expr?` 是 `fn` 体内的**控制流语法糖**（见 §3.3 desugar）。它**不允许**出现在：

1. `predicate` 体（predicate 必须纯、终止、无控制流副作用）
2. `contract` 的 `requires` / `ensures` / `invariant` / `forbid` 表达式（必须纯表达式）
3. `flow` handler 体（失败由 capability fail-closed 承载，handler 不写 `Result` 控制流；handler 调用的 capability 若返回 `Result`，则用显式 `match` 处理）
4. `workflow` 的 `return` 表达式（必须纯）

理由：`?` 的 desugar 含隐式 `return`，是控制流副作用，与"纯表达式 / fail-closed handler"两类位置的语义约束冲突。

#### 规则 R4：capability 失败的 effect 表达

capability 调用点的 `ExprEffect` 推导规则（与 spec §4.6.8 + effects.cpp 一致）**不变**：

| capability effect profile | `ExprEffect`（调用点） | 是否纯 |
| --- | --- | --- |
| `read` | `CapabilityCall` | 否 |
| `external_side_effect` / `durable_write` / `financial_write` | `ExternalEffect` | 否 |
| `unknown` | `Unknown` | 否 |

即使该 capability 的返回类型是 `Result<T,E>`，调用点的 `ExprEffect` 仍按其 effect profile 推导。即 `Result<T,E>` 是**值类型**，不改变 `ExprEffect`——这保证现有 6 级 effect 系统与 spec §4.6.8 完全兼容，无须为 `Result` 改造。

#### 规则 R5：fail-closed 与 fail-open 的边界

| 边界 | 失败处理策略 |
| --- | --- |
| `flow` handler → capability 调用 | **fail-closed**（capability 失败由 retry/compensation/recovery 承载） |
| `fn` 体内调用其他 `fn` | **fail-open**（被调用 fn 返回 `Result`，调用者用 `?`/`match` 处理） |
| `predicate` 调用 | **无失败**（predicate 是全函数，spec §3.4） |
| `workflow` `return` 表达式 | **fail-closed**（纯表达式，不允许 `Result` 控制流） |

### 8.3 与其他语言对标的依据

| 语言 | capability 失败 | 值层失败 |
| --- | --- | --- |
| **Rust** | `std::io::Error`（外部 IO 错误）+ `Result` | `Result<T, E>`（统一值通道） |
| **Swift** | `throws`（effectful，控制流） | `Result<T, Error>`（值通道） |
| **F\*** | effect `ST`/`IO`/`GT`（effect 系统） | `Result`/`option`（ADT） |
| **AHFL（本决议）** | capability effect profile + retry/compensation（effect 层） | `Result<T,E>`（值层） |

AHFL 的分层最接近 **Swift + F\*** 的混合：effectful capability 失败由 effect 系统承载（像 Swift `throws`），业务失败由 `Result<T,E>` 承载（像 Swift `Result`）。这与 AHFL 已有的 SMV failure/recovery phase 模型天然吻合，无需重做形式化后端。

### 8.4 示例

```ahfl
// ✅ 业务失败用 Result, capability 调用失败走 fail-closed
capability OrderQuery(order_id: String) -> Result<OrderInfo, OrderNotFound> {
    effect: read;
    retry: safe;
}

flow for RefundAudit {
    state Auditing with { retry: 2; retry_on: [TimeoutError]; timeout: 30s; } {
        // capability 调用: 失败 (timeout/network) 由 retry/lifecycle 承载
        // 返回值是 Result<OrderInfo, OrderNotFound>, 业务失败在值层
        let result = OrderQuery(input.order_id);
        match result {
            Ok(order)   => { ctx.order = order; goto Approved; }
            Err(NotFound) => { ctx.reason = "not found"; goto Rejected; }
        }
    }
}

// ✅ fn 体内可以用 ? 操作符 (返回类型为 Result)
fn fetch_and_check(id: String) -> Result<Bool, FetchError> {
    let order = OrderQuery(id)?;       // Err(NotFound) 被包装/透传
    Ok(order.amount > 0)
}

// ❌ 非法: 在 flow handler 内用 ? (违反 R3)
flow for Bad {
    state S {
        let order = OrderQuery(input.id)?;   // E::try_in_flow_handler
        goto Next;
    }
}

// ❌ 非法: capability 调用失败伪装成 Err (违反 R1)
capability BadQuery(id: String) -> Result<T, NetworkError>;   // NetworkError 应走 effect profile
```

新增诊断码：`E::try_in_flow_handler`（`?` 出现在 flow handler）、`E::try_in_pure_context`（`?` 出现在 predicate / contract 表达式）、`E::try_return_type_mismatch`（`?` 表达式类型与外层 fn 返回类型的 Err/None 不兼容）。

---

## 9. 开放问题 5：prelude stability policy

### 9.1 prelude 当前导出清单

`std::prelude` 是标准库显式预置模块。当前公开 PackageGraph 入口不隐式注入
prelude；用户需要显式 `import std::prelude;` 或使用全限定 std 名称。当前导出下列符号。

#### 9.1.1 类型与构造子

```text
Option, Option::Some, Option::None
Result, Result::Ok,   Result::Err
Ordering, Ordering::Less, Ordering::Equal, Ordering::Greater
List, Set, Map
String
```

#### 9.1.2 函数与方法

接收者 API 由所属类型的 inherent impl 提供：Option / Result 的 map、and_then、
unwrap_or，集合的 length、contains、get、fold，以及 String 的 trim、split 等
都使用点调用。prelude 不复制模块级 receiver wrappers，不以裸函数名选择某个
容器的实现。

free function 只保留实际独立构造、算法与格式化入口。当前导出以
`std/prelude.ahfl` 为准；不在 prelude 的独立 API 通过显式模块导入访问。

#### 9.1.3 trait

```text
Eq, Ord, Hash, Display, Debug, FromStr, FromJson, ToJson,
Iterable, Foldable, Functor
```

#### 9.1.4 不在 prelude（需显式 `import`）

| 模块 | 不在 prelude 的原因 |
| --- | --- |
| `std::time` | wall-clock 引入 `Nondet`，自动导入会污染纯表达式默认假设 |
| `std::uuid` | UUID 生成引入 `Nondet`，同上 |
| `std::json` | `JsonValue` 与领域无关，避免命名污染 |
| `std::decimal` | `Decimal` 已是 spec 原生 primitive type，但其方法（`add`/`scale_of`）非通用，按需导入 |
| `std::fmt` 的底层 `DisplayValue` | 仅 `format` 进 prelude，底层类型按需导入 |
| 任何 `@builtin` 函数 | 永远不进 prelude（永远只在 std 内部用） |

### 9.2 当前 prelude 契约

prelude 与当前 grammar / spec 同步维护，只有一份显式导出的清单。变更符号、
签名、effect 或语义时，同步更新 std、编译器消费点、测试、示例与文档；删除旧
实现和旧名称，不保留 deprecated alias、代际模块或等待周期。

#### 9.2.1 依赖与变更边界

包依赖的 SemVer 是选择依赖的数据，保留。它不选择 AHFL 的历史实现，也不要求
维护旧 prelude。破坏性变更须列出受影响符号和调用方需要修改的源码，并提供当前
contract 套件、SMV golden 和真实编译入口的回归证据。

#### 9.2.2 名字冲突解析

显式导入的 prelude 遵守 [module-resolution-rules.zh.md](./module-resolution-rules.zh.md)
中的 lookup 顺序。用户模块 local declaration 优先；prelude 不成为隐式全局依赖。
需要明确选择 std 符号时，显式导入拥有该类型 impl 的模块，然后调用 `values.map(f)`。

#### 9.2.3 同步更新流程

```mermaid
graph LR
    Contract["当前导出清单"] --> Compiler["编译器与 std"]
    Contract --> Callers["所有调用方与示例"]
    Compiler --> Checks["contract 与 SMV 回归"]
    Callers --> Checks
    Checks --> Docs["当前文档与变更说明"]
```

#### 9.2.4 行业实践的取舍

借鉴 Rust 的明确名称解析、Swift 的 stdlib 边界与 Haskell 的显式模块导入。
AHFL 不复制它们的历史 edition 或 deprecated alias 机制：当前项目明确要求
原子更新和单一实现。名称冲突规则、类型检查和形式化证据仍须完整。

---

## 10. std::time / std::uuid / std::json

### 10.1 std::time

```ahfl
module std::time;

type Timestamp;
type Duration;     // 与 spec §3.2 primitive Duration 同名, 是同一类型

// —— 构造 ——
fn now() effect Clock -> Timestamp;      // 薄包装 Clock host capability (RFC 0022 slice 1c)
fn from_unix_ms(ms: Int) effect Pure -> Timestamp;
fn duration_from_ms(ms: Int) effect Pure -> Duration;
fn duration_from_seconds(s: Int) effect Pure -> Duration;

// —— 算术/比较 ——
fn add(t: Timestamp, d: Duration) effect Pure -> Timestamp;
fn sub(t: Timestamp, d: Duration) effect Pure -> Timestamp;
fn diff(a: Timestamp, b: Timestamp) effect Pure -> Duration;
fn duration_add(a: Duration, b: Duration) effect Pure -> Duration;
fn duration_compare(a: Duration, b: Duration) effect Pure -> Ordering;

// —— 访问 ——
fn to_unix_ms(t: Timestamp) effect Pure -> Int;
fn to_unix_seconds(t: Timestamp) effect Pure -> Int;
fn as_ms(d: Duration) effect Pure -> Int;

// —— trait ——
impl Eq  for Timestamp;
impl Ord for Timestamp;
impl Hash for Timestamp;
impl Display for Timestamp;
impl Eq  for Duration;
impl Ord for Duration;
impl Hash for Duration;
impl Display for Duration;
```

### 10.2 std::uuid

```ahfl
module std::uuid;

type UUID;

fn new_v4() effect UuidV4 -> UUID;                     // v4, 薄包装 UuidV4 host capability (RFC 0022 slice 1c)
fn from_string(s: String) effect Pure -> Option<UUID>; // 包装 @builtin("uuid_from_string")
fn to_string(u: UUID) effect Pure -> String;
fn equals(a: UUID, b: UUID) effect Pure -> Bool;

impl Eq  for UUID;
impl Ord for UUID;       // 字节序
impl Hash for UUID;
impl Display for UUID;
impl FromStr for UUID;
```

### 10.3 std::json

```ahfl
module std::json;

// JsonValue 是 ADT, 表示任意 JSON 值
enum JsonValue {
    Null,
    Bool(Bool),
    Number(JsonNumber),       // 整数或浮点
    String(String),
    Array(List<JsonValue>),
    Object(Map<String, JsonValue>),
}

enum JsonNumber {
    Int(Int),
    Float(Float),
}

// —— 解析/序列化 (走 @builtin) ——
fn parse(s: String) effect Pure -> Option<JsonValue>;     // 包装 @builtin("json_parse_raw")
fn emit(v: JsonValue) effect Pure -> String;              // 包装 @builtin("json_emit_raw")

// —— 类型谓词/访问 ——
fn is_null(v: JsonValue) effect Pure -> Bool;
fn is_bool(v: JsonValue) effect Pure -> Bool;
fn is_number(v: JsonValue) effect Pure -> Bool;
fn is_string(v: JsonValue) effect Pure -> Bool;
fn is_array(v: JsonValue) effect Pure -> Bool;
fn is_object(v: JsonValue) effect Pure -> Bool;

fn as_bool(v: JsonValue) effect Pure -> Option<Bool>;
fn as_int(v: JsonValue) effect Pure -> Option<Int>;
fn as_float(v: JsonValue) effect Pure -> Option<Float>;
fn as_string(v: JsonValue) effect Pure -> Option<String>;
fn as_array(v: JsonValue) effect Pure -> Option<List<JsonValue>>;
fn as_object(v: JsonValue) effect Pure -> Option<Map<String, JsonValue>>;

fn get(v: JsonValue, key: String) effect Pure -> Option<JsonValue>;
fn at(v: JsonValue, index: Int) effect Pure -> Option<JsonValue>;

impl Eq for JsonValue;
impl Display for JsonValue { fn display(self) effect Pure -> String { emit(self) } }
impl ToJson for JsonValue { fn to_json(self) effect Pure -> JsonValue { self } }
```

### 10.4 严格解析模式（`FromJson` trait）

`FromJson::from_json` 默认返回 `Option<Self>`（宽松模式，解析失败即 `None`）。需要错误细节的场景，类型可额外提供严格版本：

```ahfl
fn from_json_strict<T: FromJson>(v: JsonValue)
    effect Pure -> Result<T, JsonError>;

struct JsonError {
    path: String,        // JSON path, 例如 ".users[3].name"
    reason: JsonErrorKind,
}

enum JsonErrorKind {
    MissingField,
    TypeMismatch { expected: String, actual: String },
    InvalidValue,
}
```

`from_json_strict` 不进 trait（避免 trait 签名二选一），由类型按需 implement 为 free function。

---

## 11. 模块分发与 prelude

### 11.1 sysroot std package

stdlib 源码以**单一权威 sysroot package** 分发。AHFL 的 std 源码位于仓库内
`std/` 目录（与 `include/` / `src/` / `grammar/` 同级），并由 `std/ahfl.toml`
描述。公开工程入口必须通过 PackageGraph 选择 sysroot：

```mermaid
flowchart TD
    CLI["CLI / LSP / formatter"]
    Opt["--sysroot <path>"]
    Env["AHFL_SYSROOT"]
    Builtin["compiled-in sysroot"]
    Manifest["<sysroot>/std/ahfl.toml"]
    Graph["PackageGraph"]
    Loader["frontend loader"]
    Error["diagnostic: failed to locate sysroot std/ahfl.toml"]

    CLI --> Opt
    CLI --> Env
    CLI --> Builtin
    Opt --> Manifest
    Env --> Manifest
    Builtin --> Manifest
    Manifest -->|exists and validates| Graph
    Manifest -->|missing / invalid| Error
    Graph --> Loader
```

`std/ahfl.toml` 的 sysroot 契约：

1. `[package].name = "std"`。
2. `[package].kind = "standard-library"`。
3. `[module].prefix = "std"`。
4. `[module].root = "."`。
5. `[prelude].module = "std::prelude"`。
6. `[prelude].injection = "explicit"`。
7. `[compiler_intrinsics].allow` 必须声明 std 内部可用的 builtin hook 前缀。

模块路径到文件路径映射沿用 [module-loading.zh.md](./module-loading.zh.md) 的
PackageGraph module root 规则：

- `std::collections` -> `std/collections.ahfl`
- `std::collections::hash_map` -> `std/collections/hash_map.ahfl`
- `std::prelude` -> `std/prelude.ahfl`

`AHFL_STD_ROOT` 和 `AHFL_STDLIB_SEARCH_ROOT` 不再是公开入口。普通项目只通过
`--sysroot`、`AHFL_SYSROOT` 或编译期默认 sysroot 选择 std。

### 11.2 prelude 显式入口

当前 public PackageGraph 入口采用显式 prelude。用户若需要 prelude 符号，应在源文件中写：

```ahfl
import std::prelude;
```

规则：

1. prelude 不自动进入 public PackageGraph 编译单元。
2. 显式 `import std::prelude;` 是普通 SourceGraph 依赖，参与 package dependency
   和 module visibility 规则。
3. 不带 alias 的 `import std::prelude;` 不向本地作用域注入简写名字；resolver 的
   prelude bare-name fallback 只有在 prelude source unit 已经被装载时才生效。
4. 局部遮蔽优先：用户模块的 local 声明遮蔽 prelude 符号（见 §9.2.2）。
5. C++ 层 `ProjectInput.inject_prelude` 只保留给内部测试和后续语法实验；它不是当前
   package manifest 语义。

### 11.3 std 模块依赖图（无环）

```mermaid
graph TD
    B["@builtin hooks<br/>(编译器内置, 冻结)"]
    O["std::option"]
    R["std::result"]
    C["std::collections"]
    CMP["std::cmp<br/>(Ordering)"]
    STR["std::string"]
    DEC["std::decimal"]
    FMT["std::fmt"]
    TIME["std::time"]
    UUID["std::uuid"]
    JSON["std::json"]
    PRE["std::prelude"]
    TRAITS["std::traits<br/>(Eq/Ord/Hash/Display/<br/>Iterable/Foldable/Functor/<br/>FromJson/ToJson/FromStr)"]

    O --> B
    R --> B
    C --> B
    STR --> B
    DEC --> B
    TIME --> B
    UUID --> B
    JSON --> B

    O --> TRAITS
    R --> TRAITS
    C --> TRAITS
    STR --> TRAITS
    DEC --> TRAITS
    JSON --> TRAITS

    C --> O
    C --> CMP
    STR --> CMP
    FMT --> STR
    FMT --> TRAITS
    JSON --> C
    JSON --> STR

    PRE --> O
    PRE --> R
    PRE --> C
    PRE --> STR
    PRE --> FMT
    PRE --> CMP
    PRE --> TRAITS
```

无环约束（RFC §3.3 依赖约束）：stdlib 模块间不允许循环依赖。`std::traits` 是叶子模块，只依赖 `@builtin` 与语言核心；`std::prelude` 是聚合模块，依赖其他 std 模块但不被其他 std 模块依赖。编译器在 resolver 阶段检测循环，违反报 `E::std_cyclic_dependency`。

### 11.4 与 RFC P6 的对应

本文档是 RFC P6（stdlib 实现 + prelude）的接口冻结。落地步骤：

1. P5 完成容器库化后，stdlib 各模块按本文签名实现
2. `std/` 目录建立，每个模块一个 `.ahfl` 文件
3. `std/ahfl.toml` 作为 sysroot package manifest 落地
4. PackageGraph sysroot 解析按 §11.1 落地
5. SMV 编码按 §4.5 表格落地
6. prelude 显式入口与稳定性按 §9.2 / §11.2 落地（CI 加入 当前 contract 回归）

---

## 12. 完整诊断码清单（本文新增）

| 诊断码 | 触发条件 | 严重级 |
| --- | --- | --- |
| `E::builtin_outside_std` | 用户模块直接调用 `@builtin` | error |
| `E::builtin_unknown` | `@builtin("name")` 的 `name` 不在冻结清单（§5） | error |
| `E::builtin_missing_effect` | `@builtin` 函数声明省略 effect 标注 | error |
| `E::try_in_flow_handler` | `?` 操作符出现在 `flow` handler 体内 | error |
| `E::try_in_pure_context` | `?` 操作符出现在 predicate / contract 表达式 | error |
| `E::try_return_type_mismatch` | `?` 表达式类型与外层 fn 返回类型的 Err/None 不兼容 | error |
| `E::effect_not_pure` | 非 `Pure` 函数进入可验证子集位置（沿用 RFC §3.4） | error |
| `E::nondet_in_invariant` | `now`/`uuid_new` 等 Nondet 函数进入 invariant（沿用 RFC §5） | error |
| `E::std_module_not_found` | std 模块在 sysroot PackageGraph module root 中找不到 | error |
| `E::std_cyclic_dependency` | std 模块间检测到循环依赖 | error |
| `W::std_root_overridden` | 预留：非默认 sysroot 覆盖了编译期 std | warning |

---

## 13. 决议小结

### 13.1 开放问题 4 决议

**结论**：`Result<T,E>` 与 capability fail-closed 错误模型**分层共存**。

- capability **调用失败**（provider 崩溃/超时/网络）走 fail-closed：由 capability effect profile + retry/compensation/recovery lifecycle 承载，不通过 `Result::Err` 编码（规则 R1）。
- capability **返回值中的业务失败**（`NotFound` / `InsufficientFunds`）走值通道 `Result<T,E>`（规则 R2）。
- `?`/`try` 仅在 `fn` 体内、返回 `Result`/`Option` 的语境下生效，禁止进入 predicate / contract 表达式 / flow handler / workflow return（规则 R3）。
- capability 调用点的 `ExprEffect` 推导规则不变，`Result<T,E>` 是值类型不影响 effect 等级（规则 R4）。
- 边界：flow handler 调用 capability 是 fail-closed；fn 体内调用其他 fn 是 fail-open；predicate 无失败；workflow return 是 fail-closed（规则 R5）。

**理由**：对标 Swift `throws`/`Result` 与 F\* effect/ADT 的分层；与 AHFL 已有的 SMV failure/recovery phase 模型天然吻合，不需要重做形式化后端；与 spec §4.6.8 的 6 级 `ExprEffect` 完全兼容。

### 13.2 开放问题 5 决议

**结论**：prelude 是当前语言导出边界，清单见 §9.1。

- prelude 导出：`Option`/`Result`/`Ordering`/`List`/`Set`/`Map`/`String`；高阶操作通过所属类型方法访问+ trait（`Eq`/`Ord`/`Hash`/`Display`/`Iterable`/`Foldable`/`Functor`/`FromJson`/`ToJson`/`FromStr`）。
- 不在 prelude：`std::time`/`std::uuid`（引入 `Nondet`）、`std::json`/`std::decimal`（领域特定）、任何 `@builtin`。
- 包的 SemVer 用于依赖选择；语言导出变化通过同次源码更新与回归闭合。
- 名字冲突：local 声明 > prelude 符号，明确选择 prelude 符号时写全限定名。
- 修改导出时同次更新所有消费点、contract 测试与文档，删除旧名称。

**理由**：对标 Rust prelude edition 边界 + Haskell `Prelude` 显式模块演进的折中；保证 stdlib 自举（RFC §3.3）与当前项目导出一致；破坏性修改明确受影响的源码。

---

## 14. 推荐阅读顺序

1. [RFC 0013](../rfcs/0013-corelib-type-system-evolution.zh.md) §3.2 类型系统四大支柱、§3.3 stdlib 表格、§3.4 可验证子集
2. [core-language.zh.md](../spec/core-language.zh.md) §4.6.8 ExprEffect 6 级、§3.4 capability、§4.6.5 capability 调用
3. [formal-backend.zh.md](./formal-backend.zh.md) "Call / Effect / Recovery Event 边界"（fail-closed 模型的 SMV 落地）
4. [module-resolution-rules.zh.md](./module-resolution-rules.zh.md) §"lookup 顺序"、§"不带 alias 的 import"（prelude 注入与名字遮蔽）
5. 本文 §8 / §9 决议、§5 `@builtin` 清单、§9.1 prelude 清单

本附件冻结后，RFC P6 的实现可直接据本文签名落地；对本文任何修改需经 RFC 评审，并通过 当前 contract 回归与 SMV golden 回归。
