# AHFL Core V0.3 Contributor Guide

本文给出面向新贡献者的 V0.3 上手路径，目标不是重复所有设计文档，而是回答三个问题：先看哪几份文档、不同类型改动应改哪些文件、最小验证该怎么跑。

关联文档：

- [project-usage-v0.3.zh.md](./project-usage-v0.3.zh.md)
- [cli-commands-v0.3.zh.md](./cli-commands-v0.3.zh.md)
- [ir-format-v0.3.zh.md](./ir-format-v0.3.zh.md)
- [backend-capability-matrix-v0.3.zh.md](./backend-capability-matrix-v0.3.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-consumer-matrix-v0.4.zh.md](./native-consumer-matrix-v0.4.zh.md)
- [native-handoff-usage-v0.4.zh.md](./native-handoff-usage-v0.4.zh.md)
- [project-descriptor-architecture-v0.3.zh.md](../design/project-descriptor-architecture-v0.3.zh.md)
- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)
- [testing-strategy-v0.3.zh.md](../design/testing-strategy-v0.3.zh.md)

## 先跑通的三条路径

建议先在本地确认三条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc
./build/dev/src/cli/ahflc check --project tests/project/check_ok/ahfl.project.json
./build/dev/src/cli/ahflc dump-ast --project tests/project/check_ok/ahfl.project.json
./build/dev/src/cli/ahflc emit-summary --project tests/project/workflow_value_flow/ahfl.project.json
```

这三条命令分别覆盖：

1. descriptor 驱动的 project model
2. project-aware frontend/debug 面
3. backend extension 参考路径

若你要进入 V0.4 handoff 路径，再额外跑：

```bash
./build/dev/src/cli/ahflc emit-native-json \
  --project tests/project/workflow_value_flow/ahfl.project.json
```

## 按改动类型找入口

### 1. 改 project model / descriptor

先读：

- [project-descriptor-architecture-v0.3.zh.md](../design/project-descriptor-architecture-v0.3.zh.md)
- [project-usage-v0.3.zh.md](./project-usage-v0.3.zh.md)
- [cli-commands-v0.3.zh.md](./cli-commands-v0.3.zh.md)

通常要改的文件：

- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/project.cpp`
- `src/cli/ahflc.cpp`
- `tests/project/`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`

### 2. 改 diagnostics / frontend debug

先读：

- [diagnostics-architecture-v0.2.zh.md](../design/diagnostics-architecture-v0.2.zh.md)
- [testing-strategy-v0.3.zh.md](../design/testing-strategy-v0.3.zh.md)

通常要改的文件：

- `include/ahfl/support/diagnostics.hpp`
- `include/ahfl/support/source.hpp`
- `src/frontend/frontend.cpp`
- `src/frontend/project.cpp`
- `src/cli/ahflc.cpp`
- `tests/project/project_*.cpp`

### 3. 改 IR / compatibility contract

先读：

- [ir-format-v0.3.zh.md](./ir-format-v0.3.zh.md)
- [ir-compatibility-v0.3.zh.md](./ir-compatibility-v0.3.zh.md)
- [ir-backend-architecture-v0.2.zh.md](../design/ir-backend-architecture-v0.2.zh.md)

通常要改的文件：

- `include/ahfl/ir/ir.hpp`
- `src/ir/ir.cpp`
- `src/ir/ir_json.cpp`
- `tests/ir/`
- `docs/reference/ir-format-v0.3.zh.md`
- `docs/reference/ir-compatibility-v0.3.zh.md`

### 4. 改 backend / 新增 backend

先读：

- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)
- [backend-capability-matrix-v0.3.zh.md](./backend-capability-matrix-v0.3.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-consumer-matrix-v0.4.zh.md](./native-consumer-matrix-v0.4.zh.md)
- [testing-strategy-v0.3.zh.md](../design/testing-strategy-v0.3.zh.md)

通常要改的文件：

- `include/ahfl/backends/`
- `src/backends/`
- `src/cli/ahflc.cpp`
- `tests/summary/` 或 `tests/formal/`
- `docs/reference/backend-capability-matrix-v0.3.zh.md`

## 最小验证清单

只要触及 V0.3 主线边界，最低建议跑：

```bash
ctest --preset test-dev -L ahfl-v0.3
```

若改动涉及全局行为，再跑：

```bash
ctest --preset test-dev --output-on-failure
```

若改动涉及 CLI / backend / 测试注册，还应先重新生成构建文件：

```bash
cmake --preset dev
```

## 文档与测试的联动约束

V0.3 当前要求文档、测试和实现保持同步：

1. 新增 project input 入口时，要同时更新 project usage、CLI commands，以及 project regression / label 注册。
   当前主要落点是 `tests/cmake/ProjectTests.cmake` 与 `tests/cmake/LabelTests.cmake`。
2. 新增 IR 字段时，要同时更新 IR format、IR compatibility 和对应 golden。
3. 新增 backend 时，要同时更新 capability matrix、backend extension guide 和至少一个 golden。
4. 新增 handoff/package 稳定字段时，要同时更新 native package compatibility 文档和对应 native golden。
5. 新增 contributor-facing 路径时，要同步更新 `docs/README.md` 与 `README.md`。

## V0.4 Handoff 入口

若你的改动落在 handoff / Native 路径，建议先读：

1. [native-handoff-architecture-v0.4.zh.md](../design/native-handoff-architecture-v0.4.zh.md)
2. [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
3. [native-consumer-matrix-v0.4.zh.md](./native-consumer-matrix-v0.4.zh.md)
4. [native-handoff-usage-v0.4.zh.md](./native-handoff-usage-v0.4.zh.md)

通常要改的文件：

1. `include/ahfl/handoff/`
2. `src/handoff/`
3. `include/ahfl/backends/native_json.hpp`
4. `src/backends/native_json.cpp`
5. `tests/handoff/`
6. `tests/native/`
7. `tests/cmake/ProjectTests.cmake`
8. `tests/cmake/LabelTests.cmake`

## 推荐阅读顺序

如果你是第一次进仓库，建议按下面顺序读：

1. [roadmap-v0.3.zh.md](../plan/roadmap-v0.3.zh.md)
2. [issue-backlog-v0.3.zh.md](../plan/issue-backlog-v0.3.zh.md)
3. [project-usage-v0.3.zh.md](./project-usage-v0.3.zh.md)
4. [cli-commands-v0.3.zh.md](./cli-commands-v0.3.zh.md)
5. [ir-format-v0.3.zh.md](./ir-format-v0.3.zh.md)
6. [testing-strategy-v0.3.zh.md](../design/testing-strategy-v0.3.zh.md)
