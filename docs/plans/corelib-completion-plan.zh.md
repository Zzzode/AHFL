# Corelib completion implementation plan

本文记录 `corelib-rfc.zh.md` 剩余 P2-P6 工作的实施顺序。目标不是重新定义范围，而是把当前已落库的 P2/P3/P5/P7 基础设施收敛到 RFC 验收状态。

## 目标边界

完成范围：

- P2：完整泛型调用、显式类型实参、闭包作为一等 `Fn` 值、闭包/函数值调用和单态化记录。
- P3：inherent method 与 trait method dispatch，包含 impl candidate resolution、歧义诊断和 trait bound 失败诊断。
- P4：`effect Pure + decreases + bounded` 可验证子集，贯穿 contract/invariant/SMV 路径；非 Pure 进入 verified context 必须拒绝。
- P5：`Optional/List/Set/Map` 从 grammar/`TypeKind` 特例迁为 stdlib nominal types，现有语法仅保留为 sugar。
- P6：实现 stdlib API 与 prelude 隐式导入，`std::collections` 的高阶 API 可通过 P2/P3 能力运行。

不在本轮范围：

- trait object / dynamic dispatch。
- higher-kinded type。
- 非 Pure 闭包。
- native gRPC / Protobuf、LLM provider、IDE 产品化等非 corelib 工作。

## 实施顺序

### 1. P2 统一调用模型

先把调用表达式从 `qualifiedIdent(args)` 扩展为统一 callable call：

- `foo<T>(args)`：显式类型实参进入 AST 和 `FnCallSiteRecord`。
- `callable(args)`：当 callee 表达式类型为 `Fn(...) -> R` 时，按 `FnT` 检查参数并返回 `R`。
- lambda typecheck 返回 `TypeKind::Fn`，参数类型从注解或期望的 `Fn` 类型获得。
- 泛型函数调用同时支持显式实参和从参数推导；显式实参优先，缺失项由推导补齐。

验收：

- `fn id<T>(x: T) -> T { x }` 可以被 `id<Int>(1)` 调用。
- `fn apply(x: Int, f: Fn(Int) -> Int) -> Int { f(x) }` 可 typecheck。
- `apply(1, \n: Int -> n + 1)` 可 typecheck，lambda 的类型为 `Fn(Int)->Int`。
- monomorphization 记录 `(id, [Int])`，重复调用去重。

### 2. P3 dispatch

在统一调用模型上实现 method dispatch，而不是给 `MemberAccessExpr` 增加临时调用分支：

- `value.method(args)` lowering 为 method-call callee 表达式，receiver 作为隐式第一个参数参与签名匹配。
- 先查 inherent impl，再查 trait impl。
- trait impl candidate 使用 nominal target symbol + type args 做精确匹配；后续再扩展 where/bound solver。
- 歧义和缺 impl 使用稳定诊断码。

验收：

- `impl<T> Foldable<T> for List<T>` 可写、可 resolution、可调用。
- `x.show()` 能定位到唯一 impl method。
- 缺 impl、重复 candidate、签名不匹配均有稳定诊断。

### 3. P4 verified subset

在 P2/P3 call target 明确后，verified subset 只消费 typed expression effect 和 signature metadata：

- `effect Pure` 函数必须有 `decreases`。
- contract/invariant/safety/liveness 中的 call target 必须在 verified subset。
- bounded refinement 作为 type metadata 进入 verified-subset gate。
- SMV encoding 只接受 verified subset 通过后的 typed program。

验收：

- `Pure + decreases + bounded` 函数可用于 invariant 并 emit SMV。
- `Nondet` 或 capability effect 函数进入 invariant 必须拒绝。
- 缺 `decreases` 的 Pure body 必须拒绝。

### 4. P5 容器 stdlib 化

按 `corelib-container-migration.zh.md` 的 P5.0-P5.11 顺序迁移：

- 先锁定 SMV golden。
- 引入 stdlib nominal container types 与 builtin hooks。
- 将 `Optional/List/Set/Map` 从 `TypeKind`/AST 特例迁为 nominal aliases。
- 语法糖 `some/none/[...]/set[...]/map[...]` 脱糖为 stdlib constructor/builtin call。
- 清理旧容器 TypeKind 和 type relation 特例。

验收：

- `TypeKind` 无 `Optional/List/Set/Map`。
- 用户显式写 `std::collections::List<Int>` 可 typecheck。
- 旧写法仍通过 prelude alias/sugar 工作。
- SMV golden 等价。

### 5. P6 stdlib 与 prelude

最后实现用户可见 API：

- `std::option`、`std::result`、`std::collections`、`std::string`、`std::time`、`std::uuid` 按 `corelib-stdlib-api.zh.md` 补齐。
- resolver 在 source unit 进入前注入 `std::prelude`，支持 opt-out 后续再定。
- 高阶 API 使用 P2 `Fn` 和 P3 trait dispatch，不保留 placeholder bodies。

验收：

- `import std::collections; fold(xs, 0, \acc, x -> acc + x)` 可用。
- 无 import 时 prelude 中的核心类型和 traits 可用。
- stdlib API contract tests 和 CLI smoke 全绿。

## 当前落地状态

已落地：

- P2：显式泛型调用、callable `Fn` 值调用、lambda 期望类型检查、lambda 参数名进入 Typed HIR，typed-HIR lowering 可生成 `ir::LambdaExpr`，top-level `fn` body 可从 `FnTypeInfo::body_block_index` 降到 IR `FnDecl::body` 并通过 IR print/JSON/verify/visitor 覆盖，runtime `CallableValue` 可捕获当前 eval context、执行 pure lambda body，并支持本地 `Fn` 值的 `f(x)` 直接调用，runtime program-level `CallEvalFn` 已可按 lowered `FnDecl::body` 执行普通函数体并保留 builtin fallback，调用点单态化记录、Typed HIR JSON 往返。
- P3：method-call AST、inherent/trait impl dispatch、impl method body typecheck、IR lowering 和 visitor/LSP/formatter 覆盖。
- P4：当前 verified subset gate 已覆盖 Pure/decreases、Nondet/capability 进入 contract/invariant/safety/liveness 的拒绝路径；bounded bool/int embedded expressions 可直接 lowering 到 SMV，非 bounded data predicate 保持 formal observation abstraction，并已有 bounded-data SMV golden 与 formal check 回归。
- P5/P6：`std::prelude` 自动注入、内建 stdlib 搜索根、`std::option`/`std::result`/`std::collections`/`std::string`/`std::time`/`std::uuid` API 骨架、`@builtin` stdlib-only/known-hook/explicit-effect guard、公开 known-hook 列表与 runtime builtin table 只读 names 枚举、stdlib `@builtin` 声明/编译器白名单/runtime table 一致性 contract 回归、覆盖 Option/Result/List/string/time/uuid 的 `ahflc check --search-root` CLI smoke、`std::collections::List<T>` 显式 nominal 类型解析、bare `Optional/List/Set/Map` 在 stdlib 可用时解析到 std nominal 类型，project-mode 已关闭容器类型语法的 legacy fallback，泛型实参统一、type relation 等价/子类型关系、Optional flow narrowing 已改用 std/legacy container view，移除这些路径上的直接 `OptionalT/ListT/SetT/MapT` 分支，`some(...)` / list / set / map literal 在无显式期望类型时优先推断为 std nominal 类型，并通过临时兼容桥接保留非项目/parse-text legacy fallback 行为、`std::option`/`std::result`/`std::collections` surface prototypes 与 builtin hook metadata、`option::{is_some,is_none,map,and_then,or_else,filter,unwrap_or,unwrap_or_else,get_or_insert}`、`result::{is_ok,is_err,map,map_err,and_then,or_else,unwrap_or,ok,err}`、`collections::{empty,singleton,length,is_empty,list_get,first,last,append,map,filter,fold,set_empty,set_singleton,set_is_empty,contains,map_empty,map_singleton,map_is_empty,contains_key,map_get}` 与 `string::{length,is_empty,concat}` 已从直接 surface builtin 迁为 AHFL wrapper body，`list_raw_*`/`set_raw_*`/`map_raw_*`/`string_raw_*` 结构性 hook 原型和调用点类型实参回归、std nominal `Option/List/Set/Map` 的 IR `TypeRef` 泛型参数输出和 response-schema validation、`some`/`none` 在 typed-HIR lowering 阶段降为 `std::option::Option::Some`/`None` ADT constructor call、list/set/map literal 在 typed-HIR lowering 阶段降为 `list_from_array`/`set_from_array`/`map_from_entries` runtime builtin call、`List`/`Map` indexing 在 typed-HIR lowering 阶段降为 `list_raw_get`/`map_raw_get` builtin call、集合高阶 API 调用和 lambda 回归、runtime builtin table 已移除 `collections_*`/`option_*`/`result_*` surface compatibility alias，仅保留集合 raw/literal 运行时原语、string、time、uuid，runtime `EnumValue` 已支持单槽 payload 以承载 Result。

仍需收尾：

- P5 完整迁移仍未完成：`Optional/List/Set/Map` 的 legacy grammar/`TypeKind` 仍作为非项目/parse-text fallback 与兼容层存在，旧容器 TypeKind 与 type relation 特例还需要最终删除。
- P6 API 目前仍未完全闭合：更完整的端到端 stdlib API contract/runtime 执行覆盖仍需继续补齐。
