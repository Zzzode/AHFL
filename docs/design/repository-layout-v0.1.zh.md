# AHFL Repository Layout V0.1

本文冻结当前仓库的目录分层与放置规则，目标是避免源码、构建脚本和测试继续回到“全部平铺”的状态。

## 目录分层

```text
AHFL/
├── CMakeLists.txt
├── cmake/
│   ├── RunExpectedFailure.cmake
│   ├── RunExpectedOutput.cmake
│   └── modules/
│       ├── AhflCompileCommands.cmake
│       ├── AhflCompiler.cmake
│       ├── AhflFormatting.cmake
│       └── AhflTesting.cmake
├── include/ahfl/
│   ├── backends/
│   ├── frontend/
│   ├── ir/
│   ├── semantics/
│   ├── support/
│   └── *.hpp              # 兼容转发头
├── src/
│   ├── backends/
│   ├── cli/
│   ├── frontend/
│   ├── ir/
│   ├── parser/
│   │   └── generated/
│   └── semantics/
├── tests/
├── docs/
│   ├── spec/
│   ├── design/
│   ├── plan/
│   ├── reference/
│   └── README.md
├── grammar/
├── examples/
└── third_party/
    └── antlr4/
```

## 文档职责边界

### `docs/spec`

放规范性、可被其他文档引用为 source of truth 的内容。

### `docs/design`

放架构、边界、实现约束和工程设计说明。

### `docs/plan`

放 roadmap、backlog、milestone 和执行计划。

### `docs/reference`

放长期参考资料，例如 how-to、glossary 和命令参考。

### `docs/README.md`

作为文档目录入口和命名规范说明，是 `docs/` 根目录下的常规例外文件。

## 源码职责边界

### `include/ahfl/support`

放最底层、无业务语义的公共基础设施：

- 所有权工具
- source range
- diagnostics

要求：

1. 不依赖 frontend / semantics / ir / backend
2. 尽量保持 header-only 或极薄依赖

### `include/ahfl/frontend` + `src/frontend`

放 surface syntax 与 parse/lowering 边界：

- 手写 AST
- parse result
- ANTLR parse tree 到 AST lowering

要求：

1. `src/frontend/frontend.cpp` 是唯一允许常规接触 ANTLR parse tree 的手写模块
2. frontend 不承载 resolver、typecheck、validate 规则

### `include/ahfl/semantics` + `src/semantics`

放静态语义阶段：

- resolver
- typecheck
- validate
- semantic type model

要求：

1. 只能消费 AST 与语义对象
2. 不直接依赖 generated parser 类型
3. 不直接承担 backend emission

### `include/ahfl/ir` + `src/ir`

放 validate 通过后的稳定中间表示与其序列化：

- recursive IR
- textual IR emitter
- JSON IR emitter

要求：

1. 不回看 parse tree
2. IR 是 backend 的稳定输入边界

### `include/ahfl/backends` + `src/backends`

放 backend driver 与 backend-specific lowering：

- `driver`
- `emit-smv`
- 后续 formal backend

要求：

1. 只消费 validate 后语义模型与稳定 IR
2. 抽象边界必须有文档，不允许藏在 emitter 实现里

### `src/cli`

放最终可执行入口：

- `ahflc`

要求：

1. 只负责参数分发和驱动流水线
2. 不吸收 backend 的实现细节
3. 后续若 CLI 继续膨胀，应优先做 backend/driver 抽象而不是继续堆在这里

### `src/parser/generated`

只放 ANTLR 生成物。

要求：

1. 不手改
2. 只通过 `scripts/regenerate-parser.sh` 更新
3. CMake 单独建 target，不与手写 frontend 源码混编到同一目录语义层

## 公共头组织规则

当前仓库同时保留两套 include 路径：

1. 模块化路径，例如 `ahfl/frontend/ast.hpp`
2. 兼容路径，例如 `ahfl/ast.hpp`

规则：

1. 新增代码优先包含模块化路径
2. 平铺头只作为兼容转发层，不继续承载真实实现
3. 若将来移除兼容头，需要先完成一次显式迁移和版本边界说明

## CMake 分层

当前构建系统按以下边界拆分：

1. 根 `CMakeLists.txt` 只负责项目入口、模块引入和顶层编排
2. `third_party/antlr4/CMakeLists.txt` 只负责 ANTLR runtime
3. `src/*/CMakeLists.txt` 分别负责各模块 target
4. `tests/CMakeLists.txt` 只负责测试注册
5. `cmake/modules/*.cmake` 放可复用的 CMake 函数

## 新增文件的放置规则

1. 新的 AST / parsing 代码放 `frontend`
2. 新的 resolver / type / validator 代码放 `semantics`
3. 新的稳定 IR 与其序列化放 `ir`
4. 新的 backend lowering 放 `backends`
5. 新的 CLI/driver 代码放 `cli`
6. 新的 generated parser 文件只放 `src/parser/generated`
7. 新的 CMake helper 不直接塞根目录，放 `cmake/modules`

## 当前已知后续演进点

1. `tests/` 目前仍是数据文件分组，后续若测试规模继续增大，可再拆成多个 `tests/*/CMakeLists.txt`
2. backend API 已在 Issue 20 中抽象为独立 driver；后续新增 backend 应继续沿用该分层，而不是把分发逻辑重新塞回 `ahflc`
3. 平铺兼容头是过渡层，不应成为长期继续堆功能的位置
