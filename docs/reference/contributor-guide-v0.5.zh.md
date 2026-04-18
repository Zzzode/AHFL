# AHFL Contributor Guide V0.5

本文给出面向新贡献者的 V0.5 上手路径，重点覆盖 package authoring、package-aware review 与 reference consumer bootstrap。

关联文档：

- [project-usage-v0.5.zh.md](./project-usage-v0.5.zh.md)
- [cli-commands-v0.5.zh.md](./cli-commands-v0.5.zh.md)
- [native-handoff-usage-v0.5.zh.md](./native-handoff-usage-v0.5.zh.md)
- [native-consumer-matrix-v0.5.zh.md](./native-consumer-matrix-v0.5.zh.md)
- [native-package-authoring-compatibility-v0.5.zh.md](./native-package-authoring-compatibility-v0.5.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](../design/native-package-authoring-architecture-v0.5.zh.md)
- [native-consumer-bootstrap-v0.5.zh.md](../design/native-consumer-bootstrap-v0.5.zh.md)
- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)

## 先跑通的四条路径

建议先在本地确认四条典型路径都可运行：

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahflc
./build/dev/src/cli/ahflc emit-native-json \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json
./build/dev/src/cli/ahflc emit-package-review \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.display.package.json
ctest --preset test-dev --output-on-failure -L v0.5-reference-consumer
```

这四条命令分别覆盖：

1. package authoring 输入进入 compiler / handoff
2. package-aware review/debug 输出
3. display name 规范化与 authoring validation
4. direct handoff reference consumer helper

## 按改动类型找入口

### 1. 改 package authoring descriptor

先读：

- [project-usage-v0.5.zh.md](./project-usage-v0.5.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](../design/native-package-authoring-architecture-v0.5.zh.md)
- [native-package-authoring-compatibility-v0.5.zh.md](./native-package-authoring-compatibility-v0.5.zh.md)
- [cli-commands-v0.5.zh.md](./cli-commands-v0.5.zh.md)

通常要改的文件：

- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/project.cpp`
- `src/cli/ahflc.cpp`
- `tests/project/`
- `tests/cmake/ProjectTests.cmake`
- `docs/reference/native-package-authoring-compatibility-v0.5.zh.md`

### 2. 改 emitted package / handoff model

先读：

- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-handoff-usage-v0.5.zh.md](./native-handoff-usage-v0.5.zh.md)
- [native-consumer-matrix-v0.5.zh.md](./native-consumer-matrix-v0.5.zh.md)

通常要改的文件：

- `include/ahfl/handoff/package.hpp`
- `src/handoff/package.cpp`
- `include/ahfl/backends/native_json.hpp`
- `src/backends/native_json.cpp`
- `tests/handoff/`
- `tests/native/`

### 3. 改 review/debug 或 reference consumer helper

先读：

- [native-consumer-bootstrap-v0.5.zh.md](../design/native-consumer-bootstrap-v0.5.zh.md)
- [native-consumer-matrix-v0.5.zh.md](./native-consumer-matrix-v0.5.zh.md)
- [backend-extension-guide-v0.2.zh.md](../design/backend-extension-guide-v0.2.zh.md)

通常要改的文件：

- `include/ahfl/handoff/package.hpp`
- `src/handoff/package.cpp`
- `include/ahfl/backends/package_review.hpp`
- `src/backends/package_review.cpp`
- `tests/handoff/package_model.cpp`
- `tests/review/`

## 最小验证清单

只要触及 V0.5 authoring / consumer 路径，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.5-package-authoring-model
ctest --preset test-dev --output-on-failure -L v0.5-package-authoring-validation
ctest --preset test-dev --output-on-failure -L v0.5-package-review
ctest --preset test-dev --output-on-failure -L v0.5-reference-consumer
```

## 文档与测试联动约束

V0.5 当前要求文档、测试和实现保持同步：

1. 改 authoring descriptor 字段时，要同步更新 authoring compatibility 文档与 `tests/project/`
2. 改 emitted package 稳定字段时，要同步更新 emitted package compatibility 文档与 `tests/native/`
3. 改 direct handoff consumer helper 时，要同步更新 consumer matrix 与 `tests/handoff/`
4. 改 review/debug 输出时，要同步更新 `tests/review/`
5. 新增 contributor-facing 路径时，要同步更新 `docs/README.md` 与 `README.md`
