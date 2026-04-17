# AHFL Core V0.2 Issue Backlog

本文将 V0.2 里程碑拆成可直接复制为 GitHub issue 的任务文本。

执行状态约定：

- `[x]` 已完成
- `[ ]` 未完成

每个 issue 都包含：

1. 标题
2. 背景
3. 目标
4. 验收标准
5. 依赖
6. 主要涉及文件

## [x] Issue 01

标题：
冻结 AHFL Core V0.2 的 project model 与 compile unit 边界

背景：
V0.1 已完成单文件 Core checker，但下一阶段若要支持多文件 source graph，需要先冻结 `source file`、`module`、`entry source`、`project`、`compile unit` 的术语和关系。

目标：

1. 明确 V0.2 仍属于 Core 路线
2. 明确 project-aware compilation 的最小边界
3. 明确哪些 filesystem/loader 细节属于 frontend，哪些不进入后续阶段接口

验收标准：

1. 新增一份 V0.2 roadmap，对 project model 有明确定位
2. 后续 issue 统一使用同一组术语
3. 文档不把 V0.2 与 Native/runtime 路线混淆

依赖：
无

主要涉及文件：

- `docs/plan/roadmap-v0.2.zh.md`
- `docs/design/source-graph-v0.2.zh.md`
- `docs/design/compiler-phase-boundaries-v0.2.zh.md`
- `docs/design/repository-layout-v0.2.zh.md`

## [x] Issue 02

标题：
冻结多文件 `module` / `import` 的装载与解析语义

背景：
语法层面已经支持 `module` 和 `import`，resolver 也已经有 import alias 和 qualified name 处理，但缺少正式冻结的多文件装载规则。

目标：

1. 明确一个 module 与 source file 的关系
2. 明确 `import` 失败、缺失 module、重复 module、alias 冲突的语义边界
3. 明确 entry source 如何触发相关 source set 的装载

验收标准：

1. 多文件设计说明中有明确的 module/file mapping 规则
2. import 解析错误与普通符号错误可区分
3. 后续实现不需要再临时决定 loader 规则

依赖：

- Issue 01

主要涉及文件：

- `docs/plan/roadmap-v0.2.zh.md`
- `docs/design/module-loading-v0.2.zh.md`
- `docs/spec/core-language-v0.1.zh.md`
- `src/frontend/frontend.cpp`
- `src/semantics/resolver.cpp`

## [x] Issue 03

标题：
冻结 V0.2 formal contract/export 边界

背景：
`emit-smv` 已冻结第一版 state-machine / workflow-lifecycle / temporal exporter，但当前仍明确不覆盖 `requires` / `ensures` 和更多 contract export 语义。

目标：

1. 明确 V0.2 formal backend 新增覆盖哪些 contract clause
2. 明确哪些 contract 语义仍保持 observation abstraction
3. 明确新增 formal export 的 IR 边界

验收标准：

1. formal backend 设计文档新增 V0.2 边界说明
2. 文档显式区分“保留语义”和“抽象语义”
3. 后续 `emit-smv` 扩张前已有单独设计约束

当前实现备注：

1. `docs/design/formal-backend-v0.2.zh.md` 已补充 V0.2 contract/export freeze 段落
2. 文档已显式拆分 contract clause export 的保留语义与 observation-backed 抽象语义
3. `requires` / `ensures`、shared `formal_observations`、以及仍未覆盖的 runtime/data semantics 边界已统一写清

依赖：

- Issue 01

主要涉及文件：

- `docs/design/formal-backend-v0.2.zh.md`
- `docs/plan/roadmap-v0.2.zh.md`
- `src/backends/smv.cpp`
- `src/ir/ir.cpp`

## [x] Issue 04

标题：
引入 project/source graph 数据模型

背景：
当前 frontend 的公开边界停在 `parse_file` / `parse_text`，更适合单文件用法；要进入 V0.2，需要一个比“单个 SourceFile”更稳定的 project/source graph 表示。

目标：

1. 建模 entry source、loaded sources、module ownership
2. 让 parser/lowering 仍然只在 frontend 内部接触 loader 细节
3. 为后续 resolver/checker 提供稳定的 source graph 输入

验收标准：

1. 新增 project/source graph 数据结构
2. 后续阶段不直接依赖文件系统遍历逻辑
3. 单文件入口仍可作为 degenerate case 正常工作

依赖：

- Issue 01
- Issue 02

主要涉及文件：

- `docs/design/source-graph-v0.2.zh.md`
- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/frontend.cpp`
- `include/ahfl/support/source.hpp`

## [x] Issue 05

标题：
扩展 frontend 与 CLI 入口，支持 project-aware 装载

背景：
即便 source graph 数据结构存在，如果 CLI 和 frontend 入口仍只接受单文件路径，V0.2 的 project-aware 边界也无法真正对外可用。当前已落地 `Frontend::parse_project(...)`、`ahflc dump-project`、project-aware `check` 与 `dump-types`，但 project-aware `emit-*` 还未接入。

目标：

1. 为 `ahflc` 增加 project-aware 入口形式
2. 让 frontend 能从 entry source 装载相关 source set
3. 保持当前单文件命令行用法兼容

验收标准：

1. `ahflc` 可以在 project-aware 模式下完成 `check` / `emit-*`
2. 单文件用法不回退
3. CLI 不直接吸收 loader 实现细节

当前实现备注：

1. `--search-root` 已可驱动 project-aware `check`、`dump-types`、`dump-project`、`emit-ir`、`emit-ir-json` 与 `emit-smv`
2. CLI 的 project-aware `check` 已接入 parse + resolve + typecheck + validate 全路径
3. `--dump-ast` 仍保持单文件模式，CLI 没有直接吸收 loader 细节

依赖：

- Issue 04

主要涉及文件：

- `src/cli/ahflc.cpp`
- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/frontend.cpp`

## [x] Issue 06

标题：
实现跨文件符号注册与 import 解析

背景：
V0.1 的 resolver 已具备 namespace、qualified name、import alias 的基本能力，但工作假设仍主要围绕单 source program。

目标：

1. 支持跨文件收集顶层声明
2. 支持跨 source 的 module/import 解析
3. 稳定 canonical name 与 symbol ownership 规则

验收标准：

1. 跨文件 type/const/capability/predicate/agent/workflow 可解析
2. duplicate module / duplicate canonical symbol / missing import 有稳定 diagnostics
3. resolver 输出可被后续 typecheck/validate 直接消费

当前实现备注：

1. `Resolver::resolve(const SourceGraph &)` 已落地
2. symbol table 已按 module 维护 local name，并保留 canonical name 全局索引
3. 已有 project resolver 回归用例覆盖基础跨文件解析和“不同 module 下重复 local type 名”场景

依赖：

- Issue 02
- Issue 04

主要涉及文件：

- `include/ahfl/semantics/resolver.hpp`
- `src/semantics/resolver.cpp`
- `include/ahfl/frontend/ast.hpp`

## [x] Issue 07

标题：
实现 project-aware diagnostics 与 source ownership 校验

背景：
一旦引入多文件 source graph，错误不再只有“当前文件中某个 range 错了”，还会出现 module/file ownership、entry 装载链、重复归属等 project-level 问题。

目标：

1. 为 project-level 错误建立稳定 diagnostics 形式
2. 区分 parser/lowering、resolver、project loader 三层错误
3. 让 diagnostics 在多文件场景下仍可回归测试

验收标准：

1. project-aware 错误不会伪装成普通 type error
2. diagnostics 可以稳定定位到相关 source file 和 source range
3. 典型 project-level 错误具备 golden tests

当前实现备注：

1. `DiagnosticBag` 已支持携带 source file 与 line/column 信息的自描述 diagnostics
2. project-aware frontend loader 与 `Resolver::resolve(const SourceGraph &)` 已为多文件错误附着 source ownership
3. 已新增 project regression，覆盖 missing import、module mismatch、missing module、duplicate module owner，以及 resolver 的跨文件 unknown type 定位

依赖：

- Issue 04
- Issue 05
- Issue 06

主要涉及文件：

- `include/ahfl/support/diagnostics.hpp`
- `src/frontend/frontend.cpp`
- `src/semantics/resolver.cpp`
- `tests/`

## [x] Issue 08

标题：
扩展跨文件 typecheck / validate 到 workflow 与 schema 场景

背景：
仅靠跨文件 resolver 还不够；V0.2 的实际价值来自 workflow、schema alias、agent target、contract target 等场景能稳定跨文件工作。

目标：

1. 让 typecheck 能处理跨文件类型与声明引用
2. 让 validate 能处理跨文件 agent/flow/workflow 关系
3. 保持现有单文件语义规则不被放松

验收标准：

1. 跨文件 workflow node target、schema alias、contract target 可稳定检查
2. 负例 diagnostics 有回归保护
3. 单文件测试继续通过

当前实现备注：

1. `TypeChecker::check(const SourceGraph &)` 与 `Validator::validate(const SourceGraph &)` 已落地
2. type/reference/expression-type 查询已带 `source_id`，避免多文件相同 range 假命中
3. 已新增 project check regression，覆盖跨文件 schema alias + flow/workflow 正例、workflow node input mismatch、以及 workflow completed final-state 校验负例

依赖：

- Issue 06
- Issue 07

主要涉及文件：

- `include/ahfl/semantics/typecheck.hpp`
- `src/semantics/typecheck.cpp`
- `include/ahfl/semantics/validate.hpp`
- `src/semantics/validate.cpp`

## [x] Issue 09

标题：
扩展 IR / JSON 到 project-aware ownership 与 declaration provenance

背景：
只要编译边界从单文件扩展到 project/source graph，稳定 IR 就不能再默认消费方天然知道声明来自哪个 source/module。

目标：

1. 在 IR / JSON 中表达 declaration 的 module/source ownership
2. 明确 project-aware declaration provenance 的稳定字段
3. 为 formal/backend consumer 保留稳定来源信息

验收标准：

1. `emit-ir` / `emit-ir-json` 可稳定表达 project-aware 来源边界
2. 新字段有文档，不依赖 AST trivia
3. golden tests 覆盖多文件正例

当前实现备注：

1. `lower_program_ir(const SourceGraph &, ...)` 与 backend driver 的 graph overload 已落地
2. textual IR 与 JSON IR 已为每个 declaration 输出稳定 provenance：`module_name`、`source_path`
3. CLI 已接入 project-aware `emit-ir` / `emit-ir-json`，并新增多文件 golden tests

依赖：

- Issue 06
- Issue 08

主要涉及文件：

- `include/ahfl/ir/ir.hpp`
- `src/ir/ir.cpp`
- `src/ir/ir_json.cpp`
- `tests/ir/`

## [x] Issue 10

标题：
扩展 formal backend 到 contract clause export 的下一层

背景：
V0.1 formal backend 明确不承诺 `requires` / `ensures` lowering。若 V0.2 要继续推进 formal 方向，需要把 contract clause export 收成受限但正式的能力。

目标：

1. 选择并实现受限的 `requires` / `ensures` export 方案
2. 明确 embedded pure expr 在 contract 语境中的映射边界
3. 保持 formal backend 仍然不是完整 runtime semantics

验收标准：

1. 文档、IR、backend emitter 三者一致
2. 新增 contract export 能被 golden 测试覆盖
3. 不把 statement/data semantics 偷渡进 `emit-smv`

当前实现备注：

1. `requires` / `ensures` 已进入共享 `formal_observations`，并使用稳定的 contract clause scope 命名
2. `emit-smv` 现已导出受限 contract clause：`requires` 作为初始态前置条件，`ensures` 作为 final-state guarded obligation
3. 已新增 / 更新 IR 与 SMV golden，用例覆盖 `requires`、`ensures` 与原有 temporal contract clause 共存场景

依赖：

- Issue 03
- Issue 09

主要涉及文件：

- `docs/design/formal-backend-v0.2.zh.md`
- `include/ahfl/ir/ir.hpp`
- `src/ir/ir.cpp`
- `src/backends/smv.cpp`

## [x] Issue 11

标题：
建立 V0.2 多文件与 backend golden 测试体系

背景：
V0.1 已有单文件 parser/checker/IR/formal golden。V0.2 的主要新风险点转向多文件装载、跨文件检查和 project-aware emission。

目标：

1. 增加多文件正例与负例 fixture
2. 覆盖 project-aware `check` / `emit-ir` / `emit-ir-json` / `emit-smv`
3. 将新测试接入 CI

验收标准：

1. `ctest` 中存在正式的多文件 regression 组
2. 负例可覆盖 missing import、duplicate module、cross-file type mismatch 等问题
3. CI 会执行 V0.2 新增测试

当前实现备注：

1. `tests/project/` 已承载多文件正例与负例 fixture，覆盖 parse / resolve / check / backend 回归
2. `tests/CMakeLists.txt` 已纳入 project-aware `check`、`emit-ir`、`emit-ir-json`、`emit-smv` 正例，以及多文件负例
3. `.github/workflows/ci.yml` 继续执行 `ctest --preset test-dev`，因此新 regression 已进入 CI

依赖：

- Issue 05
- Issue 08
- Issue 09
- Issue 10

主要涉及文件：

- `tests/`
- `tests/CMakeLists.txt`
- `.github/workflows/ci.yml`

## [x] Issue 12

标题：
补齐 project usage、CLI 与 IR reference 文档

背景：
一旦 V0.2 进入多文件/project-aware 阶段，README 级别的单文件示例已不足以表达实际用法与边界。

目标：

1. 在 `docs/reference/` 下新增 project usage 文档
2. 补齐 CLI 命令参考与 project-aware 示例
3. 补齐稳定 IR / JSON 的消费边界说明

验收标准：

1. 新贡献者可按 reference 文档跑通典型 V0.2 用例
2. CLI/project usage/IR reference 文档彼此不冲突
3. `docs/README.md` 已更新索引

当前实现备注：

1. `docs/reference/` 已新增 project usage、CLI commands、IR format 三份参考文档
2. `README.md` 与 `docs/README.md` 已链接到这些 reference 文档
3. 文档内容已覆盖 project-aware usage、命令支持矩阵、以及 IR/JSON provenance 与 `formal_observations` 的消费边界

依赖：

- Issue 05
- Issue 09
- Issue 11

主要涉及文件：

- `docs/reference/`
- `docs/README.md`
- `README.md`
