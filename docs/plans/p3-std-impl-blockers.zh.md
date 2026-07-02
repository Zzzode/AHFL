# P3 std impl 迁移前置验证记录

验证日期：2026-06-25
基于分支：develop

## 验证目标

验证 P3 审计提出的 6 个 std impl 迁移 blockers 是否成立，并给具体修复方案与
代码位置。

---

## Blocker 1: impl_fn_item 语法不支持 @builtin 属性

**结论：确认成立。**

- grammar/AHFL.g4 第 219-233 行：`builtinAttr` 规则仅被 `fnDecl`（顶层函数声明）
  引用，`implFnItem`（第 332-334 行）规则中没有 `builtinAttr?`。
- include/ahfl/compiler/frontend/ast.hpp 第 1468 行：`ImplItemSyntax::fn_def` 的
  类型是 `FnDecl*` —— `FnDecl` 结构体本身第 1352 行已经有 `builtin_name` 字段。
  所以 AST 承载能力已经具备，缺的是 grammar + AST 构建的接线。
- 影响范围：std/string.ahfl（raw_length/raw_concat/@builtin 字符串原语）、
  std/time.ahfl（wall_clock_now/duration_from_ms 等）、std/uuid.ahfl（uuid_new/
  uuid_from_string）的 impl 方法如果要保持 @builtin 必须支持。

**修复方案：**
1. grammar/AHFL.g4 implFnItem 规则前置增加 builtinAttr?
2. src/compiler/syntax/frontend/ast.cpp implFnItem visitor 把 @builtin 名称
   填到 ImplItemSyntax::fn_def->builtin_name
3. typecheck_decls.cpp 的 impl_fn 检查保留 P5 的"只允许 std 模块"约束
4. typed_hir_lower.cpp lower_impl_method 把内置 impl 方法按 BuiltinCall 降级
   （目前 TypedCallTargetKind 只有 3 个值，需要加一个 BuiltinMethod 或把
    Builtin 泛化用于 method 路径）

---

## Blocker 2: InstanceBuildKind 映射错误

**结论：确认成立。**

src/compiler/ir/typed_hir_lower.cpp 第 2710-2717 行：

```cpp
if (expr.call_target_kind == TypedCallTargetKind::InherentMethod) {
    info.kind = InstanceBuildKind::Capability;   // ← 错误：对于 std List.map 这不是 capability
    info.capability = find_capability_info(*expr.resolved_symbol);
} else {
    info.kind = InstanceBuildKind::Predicate;    // ← 错误：TraitMethod 也不该是 Predicate
    info.predicate = find_predicate_info(*expr.resolved_symbol);
}
```

对比第 2741 行普通 `fn` 调用映射为 `InstanceBuildKind::Fn`。这个错误的映射会
在泛型 std::option::and_then / std::collections::map 等通过 method-call 路径
调用时找不到 fn_info、fn_body 为空、emit 失败或生成 capability wrapper（非
语义的）。

**修复方案：**
在 TypedProgram::method_call_sites 的 source 遍历中：
1. 优先用 resolved_symbol 查 fn_info（impl 方法已经由 lower_impl_method
   在 program.fns 中注册）
2. 若找到 fn_info → InstanceBuildKind::Fn
3. 找不到（capability 方法 / 原生谓词方法）→ 保持 Capability / Predicate
4. 注意：`std::*` 前缀 stdlib 方法（第 2732 行）的跳过逻辑也需要加到
   method_call_sites 的遍历里，因为 std facade 由 RuntimeFunctionTable 提供。

---

## Blocker 3: 编译器内建类型 String/Timestamp/Duration/UUID 的 normalize_type_key

**结论：有限成立，风险可控。**

src/compiler/semantics/type_environment.cpp normalize_type_key 第 309-331 行：
- StructT/EnumT/EnumVariantT/FnT/TypeVarT → 用 symbol ID + canonical_name
- 所有其他类型（primitives）→ 回退到 `type.describe()`

include/ahfl/compiler/semantics/types.hpp StringT/UUIDT/TimestampT/DurationT 都是
空结构体，其 describe() 分别返回 `"String" / "UUID" / "Timestamp" / "Duration"`
（types.hpp 第 286-289 行）。

风险点：
- 这些 key **没有 symbol ID**，如果同一 primitive 类型在不同 symbol 表下被复
  用，describe() 的稳定性是字面量稳定，但和 nominal 类型的 key 形式不一致。
- ImplTypeInfo::target_type 对 primitive 类型：`impl String { fn method(self)... }`
  中的 String 是否会由 type_resolver 正确解析为 types::StringT，还是会试图走
  nominal `std::string::String` 路径？

验证：查 std/*.ahfl 当前字符串方法使用的是 type `String` 还是 std module 下
的 nominal struct —— 现在 std/string.ahfl 中 `fn length(self: String)` 未引
入新 struct，说明目前 String 仍作为编译内建 primitive 存在。

**修复方案：**
1. 临时方案：保持 String/Timestamp/Duration/UUID 为 primitive，impl 放于
   顶层 std 模块（孤儿规则对 inherent impl 豁免，typecheck.cpp 第 1120-1132
   行）；在 normalize_type_key 里给这 4 种 primitive 加上独立处理分支，
   输出 `"primitive:String"` 等前缀形式，避免 future primitive 名称冲突。
2. 长期（P7）：把这些内建 primitive 迁为 nominal struct（像 Option/List）。

---

## Blocker 4: 缺少 std 类型 dispatch 端到端测试

**结论：确认成立。**

- tests/unit/compiler/semantics/trait_impl.cpp 测试 user-defined Counter，
  **没有** Option/List/String/Set/Map 的 method-call 测试。
- tests/integration/trait_runtime_smoke/ 同样只有 Counter。
- tests/integration/stdlib_api_smoke/ 全部走 option:: / collections::
  自由函数路径，无 `opt.is_some()` / `xs.map(...)` / `s.length()`。

**修复方案：**
在迁移前先加一个冒烟测试 (tests/unit/compiler/semantics/std_method_dispatch.cpp
+ 一个 integration test)，验证：
1. `some(1).is_some()` 类型检查为 Bool 且 dispatch 到 inherent impl
2. `[1,2,3].length()` 为 Int
3. `"hello".length()` 为 Int
4. `Some(1).map(\x -> x+1)` 成功

---

## Blocker 5: std::uuid::parse(self: String) 归属歧义

**结论：不阻塞，按规范处理。**

该函数的 receiver 是 String 但定义在 std::uuid。自由函数形态无限制；若迁到
impl String 必须放 std::string。

**修复方案：**
移函数到 std/string.ahfl 的 `impl String {}`，返回类型 `Option<UUID>` 需要
std::string import std::option + std::uuid —— 已满足依赖图（见 spec §11.3）。
在 std/uuid 留一个 re-export 的自由函数桥接（deprecated）。

---

## Blocker 6: 命名冲突 & 历史 canonical 路径变更

**结论：不阻塞，有管理方案。**

`option::map / collections::map<T,U> / result::map<T,U,E>` 迁到 impl 后全限定
名变了。

**修复方案：**
1. 旧全限定路径在 std 模块里用 `pub use` 提供 deprecated alias 一个版本周期
2. 先加 smoke 测试跑 `.method(args)` 形态，再逐模块迁移，每迁移一个模块就
   同步更新旧调用方（typed_hir.cpp / 集成测试等）

---

## 修复顺序建议

按依赖从底层往上：

| 序号 | 修复 | 前置 |
|------|------|------|
| 1 | Blocker 3: normalize_type_key 给 primitive 加独立分支 | 无 |
| 2 | Blocker 1: impl_fn_item 支持 @builtin（grammar+AST+lowering） | 无 |
| 3 | Blocker 2: InstanceBuildKind 映射修正 | 无 |
| 4 | Blocker 4: std 类型 dispatch 冒烟测试 | 1, 2, 3 完成后 |
| 5 | Blocker 5/6: 命名管理 + deprecated alias 策略 | 随 impl 迁移一起 |
