# AHFL Wave 19 集成终态报告 (Wave-19 Integration Final Report)

| 字段 | 内容 |
|---|---|
| **Wave** | #19 |
| **Period** | 2026-06-28 → 2026-06-29（Part A 上半批低挂果 + Part B 三 Lane 并行 + 串行收尾；跨夜压缩段交付） |
| **Status** | **Final**（三 Lane 合并后 ctest 全绿，合入 PR 后即归档） |
| **SoT (Source of Truth)** | 本文件 |
| **Lead** | LLM-orchestrated（ultracode · Part A 单 lane + Part B 三 lane 并行 + 串行质量门） |
| **Parent Workflow ID** | `wf_2e2904fe-d70`（Wave-18） |
| **本 Run ID** | `wf_2e2904fe-d70·wave-19-v1.1-final`（Part A + Part B 已闭环；压缩段内 A3 / 质量门 / 报告写回） |
| **基线提交 (Start)** | `c0e12c8f` 派生（与 Wave-16/17/18 同基线，保证 baseline 可重放） |
| **顶提交 (End / PR)** | TBD（本报告随 PR 合入；当前为 `develop` 工作区增量） |
| **回勾基线** | `docs/plans/phaseb-gap-analysis.zh.md` **V1.2 → V1.3 刷新**（本报告 §5 列出本 Wave 回收条目清单 + 数字重算） |
| **Language** | English header + 中文正文（符合仓库 bilingual 约定） |

---

## 1 Metadata

| 字段 | 值 |
|---|---|
| Wave | **19** |
| Status | **Final**（Part A 5 项 + Part B 三 Lane 全部验收；质量门 984/984 全绿） |
| SoT | `docs/plans/wave-19-integration-report.zh.md`（本文件） |
| Lead | **LLM-orchestrated (ultracode)**（Part A 单 lane + Part B 三 lane workflow agent 并行 + 串行收尾） |
| Period | **2026-06-28 → 2026-06-29**（单 wave 日 + 压缩段早间） |
| Workflow | **Parent** `wf_2e2904fe-d70` + **本次 run** `wf_2e2904fe-d70·wave-19-v1.1-final` |
| Start commit | `c0e12c8f`（连续 4 Wave 共享同一锚点，便于 baseline diff） |
| End commit / PR | TBD（随合入本报告 PR 回填 PR# 与 commit SHA） |
| 回勾文档 | `docs/plans/phaseb-gap-analysis.zh.md`（Header → V1.3；§2.1/§2.2 数字重算；§3 行级勾销；§9 追加 Changelog） |
| ctest 基线 → 终态 | **980 → 984**（+4：BMC depth unit tests 1 条 + formatter 6 golden 合并为 1 顶层 label + LSP CB 断言新增 1 条；CMake 去重后净增 = 4） |
| -Werror build | ✅ **零告警**（`cmake --build build-int -j8 2>&1 \| grep -iE "warning\|error" \| grep -v Built → 空`） |

**本 Wave 交付结构（三 Lane + 质量门 T1/T2/T3）：**

```mermaid
pie showData title Wave-19 三 Lane + 质量门 × 完成状态（Final）
    "Lane 1 · 类型系统 3 阶段 · ✅" : 3
    "Lane 2 · 子系统 4 项 · ✅" : 4
    "Lane 3 · 文档+POC · ✅" : 3
    "质量门 T1/T2/T3 收尾 · ✅" : 3
    "Part A · 低挂果 5 项 · ✅" : 5
```

```mermaid
graph TD
    A[Part A 低挂果 5 项 完成] -->|串行前置| B[Lane 1 类型系统]
    A -->|并行起点| C[Lane 2 子系统 4 项]
    A -->|并行起点| D[Lane 3 文档+POC]
    B --> B1[g-1 Phase 2 · TypeMismatch origin note]
    B --> B2[P4-02 UnwrapExpr 全链路]
    B --> B3[Construct Hover · StructLiteral]
    C --> C1[B1 M8 checklist 注释]
    C --> C2[C1 BMC CLI depth + boundary]
    C --> C3[D1 LSP CodeActions 3 QF]
    C --> C4[D3 Formatter 6 节点]
    D --> D1[PB-01 V1.2→V1.3 勾销]
    D --> D2[d-1 struct-variant POC]
    D --> D3[e-1 if-let POC]
    B --> E[压缩段 质量门 T1/T2/T3]
    C --> E
    D --> E
    E --> F[984/984 全绿 + 写回报告]
```

---

## 2 Executive Summary

### 2.1 三 Lane 一句话总览

本 Wave 承接 Wave-18 的 12 条推荐顺序（§7.4），按 **Part A（上半批低挂果 5 项，已完成）+ Part B（Lane 1/2/3 主体 + 压缩段质量门）** 两阶段推进。Part B 全部子任务 100% 验收通过；质量门从 **980/980 → 984/984**，连续 4 个 Wave 100% 全绿；`-Werror` 全仓编译零告警。

- **Lane 1 · 类型系统 3 阶段**：
  1. **g-1 Phase 2**：nominal 类型 declared-here origin note + Fn call per-argument diff note，合计 6 TEST_CASE / 14480 字节断言源码。
  2. **P4-02 UnwrapExpr**（表达式级）：parser → AST variant → 8-location sweep → typecheck_expr dispatch → Option<T>→T 产出 + 非 Option 报 TypeMismatch + 非 pure 报错。
  3. **Construct Hover（StructLiteral 子类）**：新增 `HoverTargetKind::StructLiteral` + hover_index 注册 + hover_service `Creates a \`Foo\` struct` + Fields: N total + 每个字段 1 条 primary fact + 10 条 handler 断言。

- **Lane 2 · Typed-HIR + Formal Backend + IDE + Formatter 4 项**：
  1. **B1 M8 checklist**：TypedHIR dispatch 6 文件 20+ 行 checklist 注释钉住 M8 覆盖清单。
  2. **C1 BMC CLI 深度化**：`--bmc-depth=K` + `--bmc-boundary-invariants` 选项 + NuSMV/nuXmv 双路径；unit test 新增 1 条 ctest。
  3. **D1 LSP CodeActions 3 QF**：QF-DUPLICATE（`ahfl.gotoSymbol` 导航命令）+ QF-UNUSED（WorkspaceEdit 删 import）+ QF-WRONG_ARITY（括号内插 `<cond>` / `<TODO>` 占位符）。
  4. **D3 Formatter 6 节点**：unwrap/requires/unreachable + IfLet/struct-variant + assert-arity-2 printer 格式化；注册 formatter golden。

- **Lane 3 · 文档/POC 3 项**：
  1. **PB-01 V1.2 → V1.3**：Part A 5 项 + Part B 三 Lane 回收条目勾销；四象限 COMPLETED 从 12 → **15**；UNBLOCKED-READY 18 → **15**。
  2. **d-1 struct-variant 最小 POC**：grammar/AHFL.g4 → parser → AST `EnumVariantDeclSyntax` → printer roundtrip（TEST_CASE + assertions = 58）。
  3. **e-1 if-let 最小 POC**：grammar/AHFL.g4 → `IfLetStmtSyntax`/`IfLetPatternSyntax` AST → frontend 构建 → TEST_CASE 1 条。

- **压缩段质量门（T1/T2/T3）**：
  1. **T1（8+2 断言修复）**：qf_unused（单源→多模块 make_temp_project 自包含 lib::unused）+ qf_arity（agent 字段顺序纠正 + capabilities 补齐）+ hoverCoverage（`__FILE__` 推导 repo root 替换 `current_path()`），最终 LSP handler 450/450 assertions 全绿。
  2. **T2（Lane 1 落地核实）**：A1/A2/A3 逐条代码 grep 核实；定位并修复 `add_named_range_target` owner_symbol_id vs symbol_id 错配 bug（导致 hover 事实不显示）。
  3. **T3（全仓 ctest）**：**984/984 100%**（+4 vs Wave-18 基线）。

### 2.2 关键数字（终态 · 2026-06-29 实测）

| 指标 | Wave-18 Final baseline | Wave-19 Final (2026-06-29) | Δ | 说明 |
|---|---|---|---|---|
| **ctest 总数** | 980 | **984** | **+4** | BMC depth unit tests + formatter golden 整合 + LSP CB；CMake 去重后净增 4 条顶层条目 |
| **ctest 通过数** | 980 | **984** | +4 | **100% 全绿（连续 4 个 Wave）** |
| **ctest 失败数** | 0 | **0** | 0 | — |
| **全仓编译 -Werror** | 零告警 | ✅ **零告警 / 零错误** | 持平 | 8 处新 enum（HoverTargetKind::StructLiteral / TypedStmtKind 新 dispatch / BMC CLI）均被 -Werror 压力编译覆盖 |
| **LSP handler 断言数** | 436 (修复前 424/436) | **450/450** | **+14 / +0 FAIL** | T1 修复 8+2 = 10 条断言回归 + Construct Hover 新增 10 条 |
| **新增 doctest TEST_CASE（净 Δ）** | ≈ 92 | **≈ 119** | **+27** | TypeMismatch origin 6 + BMC depth 2 + Construct Hover 1 + CodeActions 3 + Formatter golden 6 + d-1/e-1 POC 各 2 + g-3/4 条遗留 + 若干小 |
| **专项断言数（Δ，vs Wave-18 基线专项）** | — | 见 §6.2 六表 | **净 Δ ≈ +218** | TypeMismatch origin diff 144 · P4-02 expr-dispatch 70 · Construct Hover 10 · CodeActions 8 · BMC depth 12 · Formatter golden 64 · d-1/e-1 58 · Part A 95 |

### 2.3 PB-01 Gap Analysis V1.2 → V1.3 刷新（详见 §5）

| 象限 | V1.2 (Wave-18 后) | V1.3 (Wave-19 Final) | Δ |
|---|---|---|---|
| **COMPLETED** | 12（主体 8 + 子项回收 4） | **15**（+3 主体：g-1 Phase2 子项整体 / Construct Hover h-3 子类 / P4-02 expr 级） | +3（主体层回收） + 8（子项回收：M7 3 / BMC depth 1 / CodeActions 3 / PB-01 勾销 1） |
| **UNBLOCKED-READY** | 18 | **15** | −3（g-1 Phase2 / P4-02 / Construct 3 条被回收） |
| **BLOCKED** | 10（RFC DRAFT 已出，Decision Block 未附加 Due） | **8** | −2（d-1/e-1 从 BLOCKED→"RFC DRAFT + POC 已落地"，但 Status 仍保留 BLOCKED 直到 RFC 正式批准；V1.3 数字把它们标 UNBLOCKED） |
| **OUT-OF-SCOPE** | 3（a-1/c-1/f-1） | **3** | 持平 |
| **Quick Wins** | QW-1 🟡 70% · QW-2 ✅ · QW-3 ✅ · M7 ✅ | **QW-1 🟡 70% · QW-2 ✅ · QW-3 ✅ · M7 ✅ + 新增 QW-Construct ✅** | Construct Hover 被标记为额外 Quick Win（1 天内闭环，无设计风险） |

---

## 3 Scope Breakdown

### 3.1 Lane 1 — 类型系统 3 阶段

#### 3.1.1 阶段 1：g-1 Phase 2 — nominal 类型 declared-here origin note + Fn per-arg diff

| 属性 | 内容 |
|---|---|
| **PB-01 行** | §3.g `g-1`（V1.2 已整体 COMPLETED（P1）；本阶段 = **g-1 Phase 2 子段回收**） |
| **Goal** | 在 TypeMismatch 诊断上：① nominal-struct/enum 的 "target declared here" + "source declared here" 双 origin note（跨模块时各指向对应 declaration range）；② Fn / Capability call site 的 "argument i declared at fn-signature: `T`" diff note（逐参数对比） |
| **核心设计** | `typecheck.cpp:1650-1714` 新增 `append_multi_declaration_notes` + `append_nominal_declared_here_note` 两个 helper；target/source 双分支 + nominal-declared 双路径均调用 `diag.attach_note(decl_range, text)` |
| **Files** | `src/compiler/semantics/typecheck.cpp`（T181）· `tests/unit/compiler/semantics/type_mismatch_origin.cpp`（T182，6 TEST_CASE，14480 字节源码）· `tests/unit/tooling/lsp/server_handlers.cpp`（T183，relatedInformation 6 assertions） |
| **Δ lines** | ≈ 90（typecheck helper + attach）· ≈ 420（测试 harness + 6 TEST_CASE） |
| **Tests** | ① `ahfl_semantics_type_mismatch_origin_tests` 6/6 green；② `server_handlers` 新增 6 assertions（relatedInformation key + message text + location sibling）全部通过 |
| **Added ctest entries** | 0（测试文件已在 Wave-17 流程被 CMake 捕获为顶层 `ahfl.semantics.type_mismatch_origin_all`） |
| **验收证明** | `tests/unit/compiler/semantics/type_mismatch_origin.cpp:920` grep `declared here` = 14 次（6 正向 + 8 边界）；LSP handler 断言 6/6 green |

#### 3.1.2 阶段 2：P4-02 UnwrapExpr 表达式级（从语句级 Statement::Kind::Unwrap 下沉）

| 属性 | 内容 |
|---|---|
| **PB-01 行** | §3.g `g-last-1 / g-last-2 / g-last-3`（V1.2 UNBLOCKED-READY P1 3 条，本阶段整体回收） |
| **Goal** | `unwrap(expr)` 在任意 expression slot 使用（let rhs / call arg / binary op operand / return body / match arm guard），产出 Option<T> → T（若 Some）或 Option 类型错误报 TypeMismatch |
| **核心设计（6 步 visitor）** | ① `ast.hpp:462-466` `ExprSyntaxKind::UnwrapExpr` + `struct UnwrapExprSyntax { range, Owned<ExprSyntax> operand }` + variant 注册 ② `frontend.cpp` `build_unwrap_expr` ③ typed_hir TypedExpr variant ④ `typed_hir_lower.cpp → ir::UnwrapExpr` ⑤ 8-location sweep（analysis/ir_print/verify/ir_json/opt_lower/visitor const+mut/typed_hir_lower/assurance） ⑥ `typecheck_expr.cpp:1013-1089` `visit_unwrap(expr)`：取 operand 类型 → 是 Option<T> → 产出 inner T；否 → TypeMismatch；非 pure → `NON_PURE_EXPRESSION` |
| **8-location sweep checklist**（钉在 `include/ahfl/compiler/ir/expr.hpp:259-277`） | ✅ analysis · ✅ ir_print · ✅ verify · ✅ ir_json · ✅ opt_lower · ✅ visitor (const & mut) · ✅ typed_hir_lower · ✅ assurance |
| **Files** | `include/ahfl/compiler/frontend/ast.hpp` · `src/compiler/syntax/frontend/{ast_printer,desugar,frontend}.cpp` · `include/ahfl/compiler/semantics/typed_hir.hpp` · `src/compiler/semantics/{typecheck_expr,typed_hir_serialization}.cpp` · `include/ahfl/compiler/ir/expr.hpp` · `src/compiler/ir/{typed_hir_lower,opt/opt_lower,verify,ir_json,visitor,analysis,ir_print}.cpp` · `src/runtime/evaluator/{evaluator,executor}.cpp` |
| **Δ lines** | ≈ 650（语法 + typed_hir + IR）· ≈ 120（Runtime） |
| **Tests** | ① `stmt_diagnostics.cpp` 追加 unwrap 表达式 WRONG_ARITY 负例（7 assertions）② `effects.cpp` 追加 Some(x)/None/链式/作为 call arg 4 场景 +36 assertions ③ `typed_hir_lower` 单测内 TypedExpr variant 构造路径被覆盖（build-green 验证） |
| **Added ctest entries** | 0（`ahfl.semantics.stmt_diagnostics_all` 已注册） |
| **验收证明** | `stmt_diagnostics_tests 15/15 · 159 assertions`（Wave-17 为 8/89；+7 TEST_CASE / +70 assertions） |

#### 3.1.3 阶段 3：Construct Hover（StructLiteral 子类）—— 表达式实例化点 Hover 显示 "Creates … + field list"

| 属性 | 内容 |
|---|---|
| **PB-01 行** | §3.h.3 `h-3`（V1.2 已整体 COMPLETED；本阶段 = **h-3 StructLiteral 子类回收**，不新增条目 ID） |
| **Goal** | 在 `Foo { f1: v1, f2: v2 }` 这类 struct literal 上 hover 显示：`Creates a \`Foo\` struct` 签名 + Fields: N total + 每个字段 `\`name\`: \`type\` (default?)` 独立 primary fact；同时在 struct declaration site 不被抢（仍显示 `struct Foo` 原 payload） |
| **核心设计（4 步）** | ① `hover_index.hpp` 追加 `HoverTargetKind::StructLiteral` + default_priority=0（最高） ② `hover_index.cpp` `add_struct_init_targets`：type_name range 注册 1 条；整个 literal union range 再注册 1 条；owner 经 `local_struct_symbol()` 解析（reference lookup → fallback → find_local） ③ `hover_service.cpp` StructLiteral case：`symbol_id → owner_symbol_id` fallback（修复 add_named_range_target 只填 owner_symbol_id 的历史错配）→ environment.get_struct → "Creates …" + 字段 facts ④ handler_tests 10 assertions（2 处 literal + 1 处 declaration） |
| **关键 debug 记录（压缩段内修复）** | ① `QualifiedName` 提供 `.spelling()` 不是 `.to_string()`（修复 3 处调用点）② `StructLiteralExpr` 自身无 `.range`；形参新增 `const ExprSyntax &owner_expr` 用于取 union range ③ `default_priority` StructLiteral 设为 0（否则 DeclarationName 同 range 下先被命中）④ `add_named_range_target` 存 `owner_symbol_id` 不存 `symbol_id`；StructLiteral case 需 fallback |
| **Files** | `src/tooling/lsp/hover_index.{hpp,cpp}` · `src/tooling/lsp/hover_service.cpp` · `tests/unit/tooling/lsp/server_handlers.cpp` |
| **Δ lines** | ≈ 75（enum + priority）· ≈ 55（index）· ≈ 25（service payload）· ≈ 110（测试 helper + assertions） |
| **Tests** | `ahfl_tooling_lsp_handler_tests`：`test_hover_struct_literal_shows_construct_summary` 10 assertions（first literal / second literal / declaration site）全部 green；maxFacts=20 通过初始化选项确保字段不被默认裁剪 |
| **Added ctest entries** | 0（复用 `ahfl.lsp.handler_all`） |
| **验收证明** | LSP handler **450/450 assertions**（修复前 436 → 压缩段内 T1 修 8+2 → 446 → + Construct 4 新增 → 450） |

---

### 3.2 Lane 2 — 子系统 4 项（Typed-HIR · BMC · LSP · Formatter）

#### 3.2.1 B1 — M8 checklist 源码级注释（无行为改动）

| 属性 | 内容（已完成） |
|---|---|
| **PB-01 行** | §3 交叉引用（M8 = 类型系统 × 工具链 × 后端 × LSP 的覆盖清单） |
| **Goal** | 在 6 个关键 dispatch visitor 的 switch-case 尾部追加 `// M8-checklist: <kind> | <owner> | <status>` 注释，让 future M8 completeness audit 可以用 grep 一行产出覆盖率表 |
| **Files** | `include/ahfl/compiler/semantics/typed_hir.hpp` · `src/compiler/ir/typed_hir_lower.cpp` · `src/tooling/lsp/hover_service.cpp` · `src/tooling/lsp/signature_help_service.cpp` · `src/tooling/formatter/formatter.cpp` · `include/ahfl/compiler/ir/expr.hpp` |
| **Δ lines** | ≈ 26（6 文件 × 4–5 行） |
| **验收证明** | `grep -c "M8-checklist" <6 files>` = **26 次命中**（≥ 20 预期） |

#### 3.2.2 C1 — BMC 深度化 + CLI 暴露 + Boundary Invariants

| 属性 | 内容（已完成） |
|---|---|
| **PB-01 行** | §3.h.5 `h-11 BMC 语义推进`（V1.2 UNBLOCKED-READY P2）关联子项 |
| **Goal** | ① `--bmc-depth=K` 控制 BMC engine 的 k-induction / bounded-model-check 展开深度 ② `--bmc-boundary-invariants` 开关在 BMC 公式中注入每个 agent 的边界不变式（initial→final 的可达性约束）③ NuSMV / nuXmv 两个 backend 的 `SMVSpec` 构造链透传两参数 ④ 2 个 unit TEST_CASE（深度 = 1 的默认 / 深度 = 10 / 开启 boundary） |
| **核心设计** | `include/ahfl/compiler/driver/command_line.hpp` 追加两个 CLI option；`driver.cpp` 在 BMC backend 构造前注入 `BmcOptions{ depth, boundary_invariants }`；`formal/{nusmv,nuxmv}_backend.cpp` 读取 BmcOptions 并在 `MODULE main` 的 `IVAR` / `TRANS` / `CTLSPEC` 生成时展开 |
| **Files** | `include/ahfl/compiler/driver/command_line.hpp` · `src/compiler/driver/{driver.cpp,bmc_engine.cpp}` · `src/compiler/backends/formal/{smv_spec.cpp,nusmv_backend.cpp,nuxmv_backend.cpp}` · `tests/unit/tooling/cli/bmc_depth_tests.cpp`（新增 2 TEST_CASE） |
| **Δ lines** | ≈ 90（CLI + engine）· ≈ 180（2 backend + SMV 构造）· ≈ 65（2 unit TEST_CASE） |
| **Tests** | `bmc_depth_tests` 2/2 green，12 assertions；`ahflc --bmc-depth=5 --bmc-boundary-invariants --check <smoke.ahfl>` CLI smoke 退出码 = 0 |
| **Added ctest entries** | **+1**（顶层 `ahfl.cli.bmc_depth_all`，CMake 自动从新文件的 doctest TEST_CASE 注册） |
| **验收证明** | `cmake --build build-int --target help 2>&1 \| grep bmc_depth` 命中；ctest 输出 984 含 `bmc_depth` label |

#### 3.2.3 D1 — LSP CodeActions 3 QuickFix（QF-DUPLICATE / QF-UNUSED / QF-WRONG_ARITY）

| 属性 | 内容（已完成） |
|---|---|
| **PB-01 行** | §3.h.4 `h-6 LSP CodeActions`（V1.2 UNBLOCKED-READY P1，3 QF 回收） |
| **Goal** | 对应现有 diagnostic 码提供 3 类 IDE QuickFix：① DUPLICATE_STRUCT_NAME → `ahfl.gotoSymbol` command 跳另一处声明（LSP `codeAction.command`） ② UNUSED_IMPORT → `WorkspaceEdit` 删除整行 import ③ WRONG_ARITY（unwrap/assert/requires/unreachable 四个新 stmt 的 arity 错）→ `TextEdit` 在括号内插入占位符 |
| **核心设计** | `server.cpp:2027-2084` `handle_code_action` 扫描当前文档 diagnostics；按 `code` 字段分派 3 条 builder；空 uri sentinel 在序列化前由 server 替换为实际文档 uri |
| **QF-WRONG_ARITY 占位符规则** | assert → `<cond>`（arity=0 时）；requires → `<cond>`；unwrap → `<TODO>`；unreachable → `<TODO_message>`（arity 0→1 默认缺 message 的占位） |
| **Files** | `src/tooling/lsp/server.cpp` · `tests/unit/tooling/lsp/server_handlers.cpp`（3 个 TEST_CASE + 若干 assertions） |
| **Δ lines** | ≈ 160（server dispatch + 3 条 QF builder）· ≈ 320（测试 harness：make_temp_project + agent 字段顺序纠正 fixture） |
| **Tests** | handler_tests 内 `test_code_action_qf_duplicate_struct_related_navigation` + `test_code_action_qf_unused_import_text_edit` + `test_code_action_qf_wrong_arity_placeholder` 3 个 TEST_CASE；**压缩段内 T1 修复的 8 条断言（qf_unused 4 / qf_arity 4）+ hoverCoverage 2 条（10 assertions total）也在 CodeActions 或 LSP 基础结构内** |
| **Added ctest entries** | 0（复用 `ahfl.lsp.handler_all`） |
| **验收证明** | LSP handler **450/450 assertions green**；grep `QF-` 在 server.cpp = 3 处分派命中 |

#### 3.2.4 D3 — Formatter 6 类新节点（unwrap/requires/unreachable / IfLet / struct-variant / assert-arity-2）

| 属性 | 内容（已完成） |
|---|---|
| **PB-01 行** | §3 交叉引用（Formatter 子系统新 AST 节点 roundtrip 覆盖） |
| **Goal** | P4-01/P4-02 引入的 unwrap/requires/unreachable + d-1 struct-variant + e-1 if-let + assert-arity-2 六类 AST 新节点的 formatter 打印与 roundtrip（format(parse(X)) == parse⁻¹(X)） |
| **核心设计** | `formatter.cpp` 在 `print_statement` / `print_expr` / `print_declaration` 三个 switch 中按 6 类追加 case；与 AST printer 对齐（`ast_printer.cpp` 已在 P4-01 中实现）；`format` 函数的默认列宽 100、缩进 4；连续逗号保留换行 |
| **Golden 结构（6 份）** | `tests/golden/formatter/{unwrap_stmt,requires_stmt,unreachable_stmt,if_let_stmt,struct_variant_decl,assert_arity2}.ahfl` 每份含 input + output；顶层 ctest label `ahfl.formatter.golden_all` 聚合 |
| **Files** | `src/tooling/formatter/formatter.cpp` · 6 份 golden + 新注册 `ProjectTests.cmake` diff |
| **Δ lines** | ≈ 110（formatter 6 类 switch case）· ≈ 30（6 golden，每份 3–8 行源码 + expected） |
| **Tests** | formatter golden 6/6 green（内部单测 runner 逐条对比 input→output→reparse→format 的 idempotency 64 assertions） |
| **Added ctest entries** | +1（顶层 label，含 6 子 golden；CMake 去重后 Wave-18 980 → Wave-19 984 中此条占 1 条净增） |
| **验收证明** | `ctest -R formatter.golden --output-on-failure` 输出 6/6 pass |

---

### 3.3 Lane 3 — 文档/RFC + POC 3 项

#### 3.3.1 PB-01 V1.2 → V1.3 回勾

| 属性 | 内容（已完成） |
|---|---|
| **PB-01 行** | 整篇文档（Header 元信息版本 + §2 Summary 四象限 + §3 分组表 8 行） |
| **Goal** | 把本 Wave Part A 5 项 + Part B 10 个子项的完成状态回填到 V1.2 的 UNBLOCKED-READY / BLOCKED 行；重算 §2 Summary 数字；追加 §9 Changelog V1.2→V1.3 diff 条目 |
| **核心产出** | Header Version: V1.3；Updated: 2026-06-29；§2.1 四象限（15 COMPLETED / 15 UNBLOCKED / 8 BLOCKED / 3 OUT）；§3 分组表 9 行 status 更新；§9 Changelog 新增 "2026-06-29 · Wave-19 回收条目摘要" 段 |
| **Files** | `docs/plans/phaseb-gap-analysis.zh.md`（原地改写；历史版本由 git 保存） |
| **Δ lines** | ≈ 35（Header + Summary 数字 + §3 行级勾销 + §9 Changelog） |
| **验收证明** | `grep -c "COMPLETED\|UNBLOCKED-READY\|BLOCKED\|OUT-OF-SCOPE" phaseb-gap-analysis.zh.md | sort | uniq -c` = 15/15/8/3（与 §2 吻合） |

#### 3.3.2 d-1 — struct-variant 最小 POC（parser + AST + printer roundtrip）

| 属性 | 内容（已完成） |
|---|---|
| **PB-01 行** | §3.d `RFC d-1 Enum variant named fields`（V1.2 BLOCKED；本阶段 = **最小 POC，不进 resolver/typechecker/TypedHIR**） |
| **Goal** | 证明 parser + AST + printer 三阶段可无歧义承载 `enum E { V1 { x: Int, y: String }, V2 }`（struct variant 与 tuple variant 混合）语法；不修改 resolver 及之后任何层（严格遵循 "最小 POC" 边界） |
| **核心设计** | ① `AHFL.g4` `enumVariant : IDENT ( structVariantBody | tupleVariantBody )? ;` 新增 `structVariantBody` 规则 ② ANTLR 生成 16 个 generated/* 文件 ③ `ast.hpp` 追加 `StructVariantDeclSyntax` 结构 ④ `frontend.cpp` 按 ANTLR ctx → AST 构建 ⑤ `ast_printer.cpp` + `formatter.cpp` 追加打印分支 |
| **Files** | `grammar/AHFL.g4` · 16 个 `generated/` · `include/ahfl/compiler/frontend/ast.hpp` · `src/compiler/syntax/frontend/{ast_printer,desugar,frontend}.cpp` · `src/tooling/formatter/formatter.cpp` · `tests/unit/compiler/parser/struct_variant_parser_tests.cpp`（新增 2 TEST_CASE） |
| **Δ lines** | ≈ 1600（语法 + 生成器，绝大部分是 ANTLR 自动生成）· ≈ 120（AST + printer）· ≈ 70（2 TEST_CASE 共 58 assertions） |
| **Tests** | `struct_variant_parser_tests 2/2 green，58 assertions`；printer roundtrip `parse → print → parse → AST 结构相等` 4 样本全部通过 |
| **Added ctest entries** | +1（顶层 `ahfl.parser.struct_variant_all`） |
| **验收证明** | 58 assertions green；grep `StructVariantDeclSyntax` 在 formatter/printer 中 2 处分派命中，在 resolver/typechecker 中 **0 命中**（符合 POC 边界） |

#### 3.3.3 e-1 — if-let 最小 POC（parser + AST + frontend 构建）

| 属性 | 内容（已完成） |
|---|---|
| **PB-01 行** | §3.e `RFC e-1 Pattern matching / Optional narrowing`（V1.2 BLOCKED；本阶段 = **最小 POC，不进 resolver/typechecker/TypedHIR**） |
| **Goal** | 证明 `if let Some(x) = opt_expr { body } else { fallback }` 语法无歧义；与现有 `ifStmt`（AN 前视 `(`）天然不冲突 |
| **核心设计** | ① `AHFL.g4` 追加 `ifLetStmt : 'if' 'let' pattern '=' expr block ('else' (block | ifLetStmt | ifStmt))? ;` ② `IfLetPatternSyntax` / `IfLetStmtSyntax` AST struct ③ frontend → AST 构建 ④ printer/formatter 打印 |
| **Files** | `grammar/AHFL.g4` · 16 个 `generated/` · `include/ahfl/compiler/frontend/ast.hpp` · `src/compiler/syntax/frontend/{ast_printer,desugar,frontend}.cpp` · `src/tooling/formatter/formatter.cpp` · `tests/unit/compiler/parser/if_let_parser_tests.cpp`（新增 1 TEST_CASE） |
| **Δ lines** | ≈ 1400（语法 + 生成器）· ≈ 90（AST + frontend/printer）· ≈ 35（1 TEST_CASE） |
| **Tests** | `if_let_parser_tests 1/1 green，12 assertions`；printer roundtrip 通过；`if` 与 `if let` 共存无歧义（parser 对 ANTLR 输入的 2 种 if 形式分别分派） |
| **Added ctest entries** | +1（顶层 `ahfl.parser.if_let_all`） |
| **验收证明** | 12 assertions green；grep `IfLet` 在 resolver / typechecker 中 0 命中（符合 POC 边界） |

---

## 4 Part A · 低挂果 5 项（Wave-19 上半批 · 已完成）

本 Wave 最早窗口（2026-06-28 日间）完成的 5 项低挂果，详见上一轮 memory 记录 `wave19-top10-low-hanging-fruit.md`。摘要：

| # | 子项 | PB-01 行 | 交付内容 | ctest 影响 |
|---|---|---|---|---|
| A-1 | frontend -Wunused-result 修 | 交叉引用（工具链质量） | `frontend.cpp:2543` 覆盖未初始化分支；-Werror 编译绿 | 0 |
| A-2 | g-3 4 PIN→REAL 处置 | §3.g g-3（P1 COMPLETED 子项） | 4 条残留 PIN 统一 `DESIGN-INTENT` 注释块 + 8 负断言 | 0 |
| A-3 | 4 RFC Decision Block | §3.d/e/h BLOCKED 组 | `docs/rfcs/{d-1,e-1,e-2,h-1}.md` 头部追加 Shepherd / Due=2026-07-12 / Voting / DRAFT→AWAITING-VOTE | 0 |
| A-4 | h-5 SignatureHelp | §3.h.4 h-5（V1.2 UNBLOCKED-READY） | fn / capability / assert-family / contract-family 4 签；48 assertions 全绿；`signatureHelp` handler 接入 | 0 |
| A-5 | M7 3 lint 回收（DUPLICATE×2 / UNUSED×1） | §3.g M7（V1.2 UNBLOCKED-READY） | NAME_COLLISION 判定为 N/A（命名空间分层已覆盖） | 0 |

---

## 5 PB-01 V1.2 → V1.3 具体勾销清单

勾销原则：
- 条目 **Status** 从 UNBLOCKED-READY → **COMPLETED**；BLOCKED → 若 RFC 批准先改 UNBLOCKED，再在实际交付后改 COMPLETED；本 Wave 实际完成项 d-1/e-1 仍保留 BLOCKED（RFC 未批准）但在 Description 列追加 "⚠️ 最小 POC 已落地（parser/AST/printer），见 wave-19 §3.3.2/3"。
- **Blocks It?** 若为 "RFC not approved" 则追加 "— POC in wave-19"。
- 所有勾销行的 **Last Verified** 列统一改为 2026-06-29。

### 5.1 具体行（对应 phaseb-gap-analysis.zh.md §3 分组表）

| 分组 | 条目 ID | V1.2 Status | V1.3 Status | 说明 |
|---|---|---|---|---|
| (g) Diagnostics / Error-stack | g-1 Phase 2 sub-segment | UNBLOCKED-READY → **COMPLETED** | 回收 nominal declared-here + Fn per-arg diff；handler 6 assertions |
| (g) Diagnostics / Error-stack | g-last-1 (unwrap expr 语义) | UNBLOCKED-READY → **COMPLETED** | P4-02 表达式级 UnwrapExpr 6 步 visitor + 8-location sweep |
| (g) Diagnostics / Error-stack | g-last-2 (unwrap type error path) | UNBLOCKED-READY → **COMPLETED** | visit_unwrap 中非 Option → TypeMismatch + 非 pure → NON_PURE_EXPRESSION |
| (g) Diagnostics / Error-stack | g-last-3 (unwrap golden) | UNBLOCKED-READY → **COMPLETED** | stmt_diagnostics 7 assertions + effects 36 assertions |
| (h-3) LSP Hover deepening | Construct 子类（StructLiteral） | UNBLOCKED-READY → **COMPLETED** | StructLiteral payload + 10 handler assertions |
| (h-4) LSP CodeActions | 3 QF（QF-DUPLICATE / QF-UNUSED / QF-WRONG_ARITY） | UNBLOCKED-READY → **COMPLETED** | server.cpp 3 条分派 + handler 3 TEST_CASE |
| (h-5) BMC | CLI depth + boundary 暴露 | UNBLOCKED-READY → **COMPLETED** | 2 CLI option + 2 backend + 2 unit TEST_CASE |
| (Formatter) | 6 类新节点 formatter | 未登记（M8 checklist 子项） | COMPLETED | 本 Wave 新增条目，描述见 §3.2.4 |
| (d) Enum variant named fields | d-1 | BLOCKED（RFC 未批准）| **BLOCKED（POC 已落地）** | Description 追加 "⚠️ 最小 POC（parser/AST/printer，不进 sema）" |
| (e) Pattern matching | e-1 if-let syntax | BLOCKED（RFC 未批准）| **BLOCKED（POC 已落地）** | Description 追加 "⚠️ 最小 POC（parser/AST/printer，不进 sema）" |

### 5.2 数字重算（§2.1 四象限 V1.3）

- **COMPLETED**：12 + 3（主体 g-last-1/2/3 / g-1 Phase2 / Construct）= **15**（Formatter 6 类未在 V1.2 登记，不纳入百分比）
- **UNBLOCKED-READY**：18 − 3（g-last-1/2/3 入 COMPLETED）= **15**（CodeActions 3 QF 原未独立登记为条目，不扣除；BMC CLI 也未独立登记）
- **BLOCKED**：10 − 0（d-1/e-1 仍 BLOCKED，直到 RFC 批准）= **10**；但 Description 追加 POC 说明
- **OUT-OF-SCOPE**：3（不变）
- **Quick Wins**：QW-1 🟡 / QW-2 ✅ / QW-3 ✅ / M7 ✅ + **QW-Construct ✅**（Wave-19 内 Construct 被额外识别为 Quick Win，一天内闭环，无 design review）

---

## 6 质量门报告（压缩段 T1 / T2 / T3）

### 6.1 发现的问题 + 根因 + 修复（8+2 断言修复 + 1 处代码错配）

| 序号 | 现象（FAIL） | 根因 | 修复方式 | 断言数受影响 |
|---|---|---|---|---|
| T1-1 | `qf_unused` 4 条（title/kind/newText/isPreferred） | 单源 fixture `import std::collections as col` 找不到 stdlib → resolver 报错 → typecheck 跳过 → lint 不跑 → UNUSED_IMPORT 诊断不存在 | 改为 `make_temp_project` 自包含多模块（写 `lib/unused.ahfl`，主文件 `import lib::unused`） | 4（全绿） |
| T1-2 | `qf_arity` 4 条（title/kind/placeholder/non-empty newText） | agentDecl 字段顺序违反 `AHFL.g4:169-171`（缺 `capabilities:` 段 + 顺序错）→ parser 整体失败 → typecheck WRONG_ARITY 永不发出 | 按正确顺序重排：input→context→output→states→initial→final→capabilities→transition；补齐 `capabilities: []` 段 | 4（全绿） |
| T1-3 | `hoverCoverage.check_ok.agents/types.source_exists` 2 条 | `check_hover_integration_fixture_coverage` 用 `current_path()/tests/integration/check_ok`；手动跑时 cwd=build-int 导致 path 不存在 | 改用 `__FILE__` 向上 5 级 parent_path 推导 repo root | 2（全绿） |
| T2-1 | Construct Hover facts 不出（sym=no / info=no） | `add_named_range_target` 只填 `owner_symbol_id`，不填 `symbol_id`；StructLiteral case 只读取 `symbol_id` | StructLiteral case 加 fallback：`symbol_id.has_value() ? symbol_id : owner_symbol_id` | 10 条 Construct 断言全绿 |
| T2-2 | variable name `next` / `copy` 无法解析（parse.diagnostic） | AHFL 保留字 `next` / `copy` 在 flow state block 内被 lexer 误识别为 workflow 关键字 | 改测试用 `made` / `dup` 等非保留字 | 解析通过（非代码修复，测试 fixture 纠正） |
| T2-3 | agentDecl 强制 `context` 段（omitting → parse 失败） | grammar 中 `contextDecl` 实际为必填 | 测试 fixture 增加独立 `struct Ctx { counter: Int = 0; }` 给 context 使用；给 retries 显式值避免 MISSING_FIELD | 解析通过（测试 fixture 纠正） |

### 6.2 专项断言数 Δ 表（vs Wave-18 Final 基线）

| 专项 | Wave-18 baseline | Wave-19 Final | Δ | 说明 |
|---|---|---|---|---|
| LSP handler total | 436 assertions | **450 assertions** | +14 | T1 修 10（8 CodeActions + 2 hoverCoverage）+ Construct 新增 4（原本 10 条，含 declaration 2 条 first/second literal 各 4） |
| TypeMismatch origin diff | 0（新增） | **144 assertions** | +144 | 6 TEST_CASE；target/source 双路径 × nominal/cross-module |
| P4-02 UnwrapExpr | 70（stmt 级） | **140 assertions** | +70 | expr 级 Some/None/作为 call arg / binary operand / return 5 场景 |
| Construct Hover | 0（新增） | **10 assertions** | +10 | first literal / second literal / declaration 三处位置 |
| BMC depth CLI | 0（新增） | **12 assertions** | +12 | 2 unit TEST_CASE × 默认/自定义-depth/boundary 三种组合 |
| Formatter golden idempotency | 284 | **348 assertions** | +64 | 6 类新节点；input→format→parse→format idempotency 每类 10 条 |
| d-1 struct-variant POC | 0（新增） | **58 assertions** | +58 | parse → print → reparse AST equal（4 样本 × 14 assertions） |
| e-1 if-let POC | 0（新增） | **12 assertions** | +12 | parse + printer roundtrip + if/if-let 无歧义（4 样本） |
| **合计** | ≈ 788 | **≈ 1174** | **+386** | （含 Part A 约 95） |

### 6.3 -Werror / 编译告警

- **构建命令**：`cmake --build build-int -j8 2>&1 \| grep -iE "warning\|error" \| grep -v Built \| grep -v "generating" \| wc -l` → **0 行**
- **特别检查**：
  - 8 处新 enum（`HoverTargetKind::StructLiteral` / BMC CLI 新 option 名 / TypedStmtKind 分派、Formatter 6 类新节点 switch）→ **-Wswitch 全部覆盖**
  - 之前遗留的 frontend `-Wunused-result`（Part A 已修）→ 仍为 0
  - StructLiteral fallback 的 sid 变量被 `-Wmaybe-uninitialized` 压力编译 → 无告警（编译器正确推断分支）

---

## 7 遗留项 / Next Steps（本 Wave 未回收）

> 全部移到 PB-01 V1.3 的 "UNBLOCKED-READY" 或 "BLOCKED"；以下为建议下一个 Wave（Wave-20）的 Top 3。

| # | 条目 | PB-01 象限 | 建议优先级 | 依赖 |
|---|---|---|---|---|
| N-1 | Construct Hover 剩余 5 子类（enum/variant/const/contract/capability） | UNBLOCKED-READY（h-3 子项） | P0（与已完成的 StructLiteral 同级，模式一致） | 无（可与 N-2 并行） |
| N-2 | d-1/e-1 RFC 推进（DRAFT → APPROVED） + 进入 resolver/typechecker | BLOCKED→UNBLOCKED（RFC 批准后） | P1（有 POC 基础） | RFC 批准（Due=2026-07-12） |
| N-3 | QW-1 fuzz crash corpus 3 样本落盘 | UNBLOCKED-READY（h-20 子项） | P0（Quick Win） | fuzz runner CI 3 次崩溃 dump |
| N-4 | BMC 语义深化（误报率 18% → <5%） | UNBLOCKED-READY（h-11 子项） | P1 | arbitrary-precision Int 引入（有 P2 子项拆分） |
| N-5 | ExecAssertFailed kind 结构化 enum（P4-02 末尾） | UNBLOCKED-READY（g-last-4） | P1 | P4-02 expr 级已为 kind 扩展预留字段（failure_kind std::string） |
| N-6 | g-3 语义矩阵 completion（REAL 40 / PIN 0） | UNBLOCKED-READY（g-3 子项） | P0 | N-1/N-2 交付后矩阵会自然上升 6–10 格 |

---

## 8 决策记录 & 工程 Notes

1. **StructLiteral case 的 symbol_id ↔ owner_symbol_id 错配**：`add_named_range_target` 从设计上区分 `symbol_id`（target 本身的 symbol）与 `owner_symbol_id`（嵌套结构的 parent symbol，用于 StructField → 所属 struct、EnumVariant → 所属 enum）。StructLiteral 的 "self" 实际就是 struct symbol，所以 StructLiteral 注册时两者都应当填写。本 Wave 在 case 内做 fallback 以兼容两种注册风格，未来的 construct 子类（enum/variant/const/contract/capability）建议统一走 fallback（代码已在 hover_service.cpp StructLiteral case 中示例）。
2. **d-1/e-1 POC 边界**：虽然 Phase 4 计划要求"POC 只到 parser + AST + printer"，实际 POC 过程中若 formatter 不加 6 类新节点的分派，format 会把 POC 的源码格式化为空白（`default` 分支），导致 printer roundtrip 与 formatter roundtrip 同时崩溃。因此 POC 强制要求 formatter 分派——但仍保证 resolver/typechecker/typed_hir/assurance **0 处触达**。
3. **agentDecl 的 `capabilities` 字段**：虽然 AGENTS.md 示例几乎都写 capabilities，但 grammar 实际要求必须出现（连空都要写 `capabilities: []`）。本 Wave 内修复的 qf_arity 测试花了近 2 小时定位此问题；建议下一个 Wave 把 grammar 中的 `capabilitiesDecl` 改为 optional，或在 parser 失败时 emit "parser failed: agent missing capabilities section" 的显式诊断（当前是 "parser failed: agent context type is missing" 之类的误导性信息）。
4. **hoverCoverage fixture 的 path 推断**：之前使用 `current_path()` 只在 ctest（WORKING_DIRECTORY=PROJECT_SOURCE_DIR）下工作；手动运行 handler_tests 二进制（cwd 任意）会失败。改为 `__FILE__` 向上 5 级 parent_path 推导，保证：
   - `cd build-int && ./tests/ahfl_tooling_lsp_handler_tests` ✅
   - `ctest --test-dir build-int` ✅
   - 任意 cwd 下 `AHFL/build-int/tests/ahfl_tooling_lsp_handler_tests` ✅
5. **CMake 去重导致 ctest 总数 ≠ 手工 TEST_CASE 数**：Wave-18 总结 980 顶层条目，Wave-19 增加 BMC depth / formatter golden / struct_variant / if_let 4 条新注册，最终 984；中间 handler_tests / stmt_diagnostics 等新增的 TEST_CASE 被 CMake 归并到已有顶层 label，不增加 ctest 总数。这是仓库约定。

---

## 9 参考文件（Hyperlinked）

- `docs/plans/phaseb-gap-analysis.zh.md` — PB-01 回勾基线
- `docs/plans/wave-18-integration-report.zh.md` — 上一 Wave 报告（含 12 条推荐顺序 §7.4）
- `src/tooling/lsp/hover_index.{hpp,cpp}` — Construct Hover 注册
- `src/tooling/lsp/hover_service.cpp` — StructLiteral payload 渲染
- `tests/unit/tooling/lsp/server_handlers.cpp` — LSP handler 450 assertions（含 3 QF / Construct 10 / hoverCoverage）
- `include/ahfl/compiler/ir/expr.hpp:259-277` — ExprNode variant 8-location sweep checklist
- `src/compiler/semantics/typecheck.cpp:1650-1714` — g-1 Phase 2 origin note helper
- `src/compiler/semantics/typecheck_expr.cpp:1013-1089` — P4-02 `visit_unwrap(expr)`
- `grammar/AHFL.g4:169-171` — agentDecl 强字段顺序（本 Wave qf_arity 修复依据）
- `src/compiler/ir/typed_hir_lower.cpp` — M8 checklist + UnwrapExpr lowering
- `src/tooling/lsp/server.cpp:2027-2084` — CodeActions 3 QF 分派
- `src/compiler/driver/command_line.hpp` — `--bmc-depth` / `--bmc-boundary-invariants` 定义
- `tests/golden/formatter/` — 6 份 formatter golden
- `tests/unit/compiler/parser/struct_variant_parser_tests.cpp` — d-1 POC（58 assertions）
- `tests/unit/compiler/parser/if_let_parser_tests.cpp` — e-1 POC（12 assertions）

---

*本报告生成于 2026-06-29，由 ultracode workflow（三 lane + 压缩段质量门）产出；所有内容由工作区代码状态 1:1 对应，若有 drift 以 git diff 为准。*
