# AHFL CLI 工作流

使用 `ahflc` 检查和格式化源码、查看执行计划、演练和运行 workflow。本指南按任务列出常用命令；完整参数见 [CLI 命令参考](../reference/cli-commands.zh.md)，工程配置见 [工程配置指南](./packages.zh.md)，模型服务配置见 [执行指南](./execution.zh.md)。

## 安装与约定

本仓库开发构建使用：

```bash
cmake --preset dev
case "$(uname -s)" in
  Darwin) BUILD_JOBS=$(sysctl -n hw.ncpu) ;;
  *) BUILD_JOBS=$(nproc) ;;
esac
cmake --build --preset build-dev -j"$BUILD_JOBS"

REPO_ROOT="$PWD"
AHFLC="$PWD/build/dev/src/tooling/cli/ahflc"
```

以下命令假定当前目录是 repository root。已安装工具链可把 `AHFLC` 替换为 PATH 中的
`ahflc`，并将 `--sysroot` 指向工具链根或其 `std/ahfl.toml`。

```bash
"$AHFLC" --help
"$AHFLC" check --help
```

## 先选输入模式

工具链首先需要知道你操作的是一个 detached 文件、一个 package 还是 workspace：
| 模式 | 何时使用 | 入口 |
|---|---|---|
| detached 单文件 | 最小语法试验；只使用 primitive 或本文件声明 | `ahflc check path/to/file.ahfl` |
| source 自动发现 | 文件位于某个 `ahfl.toml` 之下 | `ahflc check src/main.ahfl` |
| 单 package | 明确选择 manifest 和 target | `--manifest ahfl.toml --target workflow` |
| 多 package workspace | 明确选择 workspace member | `--workspace ahfl.workspace.toml --package name --target workflow` |

detached 文件不能隐式 `import std` 或其他 package。需要 `std`、跨模块 import、target、
run 或 native handoff 时，使用 PackageGraph：
```bash
"$AHFLC" check \
  --manifest examples/execution-demo/ahfl.toml \
  --target workflow \
  --sysroot .
```

包图、workspace 与 sysroot 的详情见 [工程配置](./packages.zh.md)。

## 命令地图

| 任务 | 首选命令 | 产物 / 下一步 |
|---|---|---|
| 检查源码与工程 | `check` | 修复 parse/resolve/typecheck/validate diagnostics |
| 统一源格式 | `fmt` / `fmt --check` | 本地改写或 CI blocking check |
| 查看解析与类型 | `dump ast`、`dump types` | 诊断源代码理解是否偏离预期 |
| 查看工程图 | `dump package-graph`、`dump lockfile` | 检查 package、dependency、sysroot、lockfile |
| 审查 workflow 结构 | `emit summary`、`emit execution-plan` | 核对 Agent、DAG、input/return reads |
| 演练而不调用外部系统 | `emit dry-run-trace` | 使用 capability mocks 检查执行顺序 |
| 发射交接/验证 artifact | `emit native-json`、`emit smv`、`emit assurance-json` | 进入下游系统、formal 或 assurance 审查 |
| 运行真实 workflow | `run` | 使用 human/JSON/JSONL 输出 |
| 检查效果治理 | `validate` | assurance blockers 或 ready |
| 验证有限控制模型 | `check --verify=on` | nuXmv/NuSMV 结果与 counterexample |
| 管理 package 生命周期 | `init`、`package`、`registry`、`emit public-api*` | 开发者/生态入口，先阅读命令 reference |

命令的主要形态：
```text
ahflc check [options] [<input.ahfl>]
ahflc run [options]
ahflc fmt [--check] <input.ahfl|dir>...
ahflc dump ast|types|package-graph|lockfile [options]
ahflc emit <artifact> [options] [<input.ahfl>]
ahflc validate [options] [<input.ahfl>]
ahflc check --verify=on [options] [<input.ahfl>]
```

`run` 对 package 项目可直接使用 `ahflc run`；当 manifest 有 `[run]` 时，会发现
target、input、LLM config、output format 与 verbosity。没有 `[run]` 时，至少提供
`--input` 或 `--input-file`，并按需要显式指定 workflow/config。

## 每日开发闭环

对 `examples/execution-demo` 或自己的 package，推荐固定采用以下顺序：
```bash
"$AHFLC" check --manifest examples/execution-demo/ahfl.toml \
  --target workflow --sysroot .

"$AHFLC" fmt --check --manifest examples/execution-demo/ahfl.toml --sysroot .

"$AHFLC" dump package-graph --manifest examples/execution-demo/ahfl.toml --sysroot .

"$AHFLC" emit summary --manifest examples/execution-demo/ahfl.toml \
  --target workflow --sysroot .

"$AHFLC" emit execution-plan --manifest examples/execution-demo/ahfl.toml \
  --target workflow --sysroot .
```

结果解释：
| 命令 | 重点检查 |
|---|---|
| `check` | source range diagnostic、import、类型、Agent/flow/workflow 结构 |
| `fmt --check` | 格式化是否可重复；CI 不需要改写工作树 |
| `dump package-graph` | package identity、sysroot、source units、target export |
| `emit summary` | 声明数量、flow summary、workflow input/return reads |
| `emit execution-plan` | entry workflow、dependency edges、node lifecycle 和 input reads |

## Format、Dump 与 Emit

### Formatter

`fmt` 可接收文件、目录、manifest 或 workspace。默认原地格式化；`--check` 只检查并在有
差异时返回非零：
```bash
"$AHFLC" fmt src/main.ahfl
"$AHFLC" fmt --check src/
"$AHFLC" fmt --manifest examples/execution-demo/ahfl.toml --sysroot .
"$AHFLC" fmt --workspace workspace/ahfl.workspace.toml --package support --sysroot .
```

配置从输入路径向上查找 `.ahfl-format`。目前支持 `indent_width`、`use_tabs`、
`max_line_length`、`trailing_newline`、`align_fields` 与 `sort_imports` 等键。formatter
对当前覆盖语法保证无损/幂等；新增或罕见语法应先用 `fmt --check` 验证再批量写回。

### Dump

```bash
"$AHFLC" dump ast examples/execution-demo/src/main.ahfl
"$AHFLC" dump types examples/execution-demo/src/main.ahfl
"$AHFLC" dump package-graph --manifest examples/execution-demo/ahfl.toml --sysroot .
"$AHFLC" dump lockfile --manifest examples/execution-demo/ahfl.toml --sysroot .
```

`dump ast` 排查解析形状，`dump types` 排查符号/类型环境，`dump package-graph` 排查
工程构造。不要把 dump 文本作为机器接口；机器消费者应使用相应 JSON artifact 或
`run --output-format jsonl`。

### Emit artifact

按需要选择输出产物（artifact）：
| 类别 | Artifact | 推荐用途 |
|---|---|---|
| 编译与诊断 | `ir`、`ir-json`、`core-ir-json`、`opt-ir`、`opt-ir-json`、`summary` | 编译器诊断、IR 研究、优化检查 |
| workflow 交接 | `native-json`、`execution-plan`、`dry-run-trace`、`package-review` | PackageGraph/handoff/DAG 审查 |
| assurance/formal | `smv`、`smt`、`assurance-json` | 交给 `check --verify=on` / `validate` 或归档 |
| package API | `public-api`、`public-api-docs`、`public-api-diff` | 包兼容性与 SemVer 审查 |
| 基础设施产物 | `k8s-crd`、`openapi`、`terraform`、`wasm` | 依赖构建时启用的后端；输出与限制见 [后端能力参考](../reference/backend-capability-matrix.zh.md) |

```bash
"$AHFLC" emit ir-json examples/execution-demo/src/main.ahfl
"$AHFLC" emit native-json --manifest examples/execution-demo/ahfl.toml \
  --target workflow --sysroot .
"$AHFLC" emit package-review --manifest examples/execution-demo/ahfl.toml \
  --target workflow --sysroot .
"$AHFLC" emit smv examples/refund/audit.ahfl
```

`dry-run-trace` 需要 package target、`--capability-mocks` 与 `--input-fixture`。详见
[执行与包指南](./execution.zh.md)，不要用真实 LLM run 替代 deterministic
dry run。

## Run 的命令层

真实运行前，先在 manifest 配置 `[run]`：
```toml
[run]
target = "workflow"
input = "inputs/high-severity.json"
# 未指定 llm_config 时使用 ~/.ahfl/llm_config.json。
# 需要包内独立配置时才显式填写，例如 llm_config = "llm_config.example.json"。
output_format = "human"
verbosity = "normal"

[run.profiles.low-risk]
input = "inputs/low-risk.json"
output_format = "jsonl"
verbosity = "trace"
```

命令行优先级是：**显式 CLI 参数 > 选中的 profile 字段 > `[run]` 默认字段**；LLM 配置
的解析顺序为 `--llm-config` > profile `llm_config` > `[run].llm_config` >
`~/.ahfl/llm_config.json`。provider 只需在全局文件配置一次，之后裸跑即可。例如：
```bash
cd examples/execution-demo

# 使用 [run] 默认值；LLM 配置来自 ~/.ahfl/llm_config.json（一次性配置）。
../../build/dev/src/tooling/cli/ahflc run

# 用 low-risk profile 覆盖 input/output/verbosity。
../../build/dev/src/tooling/cli/ahflc run --profile low-risk

# 显式 CLI 输出格式再次覆盖 profile。
../../build/dev/src/tooling/cli/ahflc run --profile low-risk --output-format json

# 后续命令从仓库根目录执行。
cd "$REPO_ROOT"
```

完整 provider、secret、预算、tool/binding、JSON schema 与 event 输出说明见
[执行与包指南](./execution.zh.md)。运行自动化时推荐：
```bash
"$AHFLC" run --manifest examples/execution-demo/ahfl.toml \
  --profile low-risk \
  --output-format jsonl \
  --verbosity trace \
  --sysroot . > run.events.jsonl
```

## Assurance 与形式化验证

```bash
"$AHFLC" validate tests/golden/assurance/ok_effects.ahfl

"$AHFLC" check --verify=on --formal-backend nuxmv \
  --model-checker /path/to/nuXmv \
  --checker-timeout-seconds 60 \
  --formal-model-out build/refund.smv \
  examples/refund/audit.ahfl
```

`validate` 检查 effect profile、idempotency、receipt、approval、compensation 等
assurance obligation。`check` 默认调用 NuSMV/nuXmv 验证生成的**有限控制模型**；`spin` 与
`tlaplus` 当前会报告 verification unsupported。它们不证明 provider 内部实现或外部业务
状态。详细边界见 [行为约束与验证](./assurance.zh.md)。

普通数据 requires 是环境前提，ensures/invariant/forbid 默认 not_requested。需要业务规则公式证明时显式运行：

```bash
"$AHFLC" check --manifest examples/refund-rules/ahfl.toml --verify-data-contracts
```

此选项需要开启验证。数据目标不支持、结果未知、出现反例、前提矛盾或缺少求解器时会失败。`contract_implication` 表示在 `requires` 前提成立时目标成立，不能据此判断 flow 或外部服务实现正确。状态字段和处理步骤见 [行为约束与验证](./assurance.zh.md)。

## Package 与 Registry 命令

初始化或分发包时使用：
```text
ahflc init --single-file <input.ahfl>
ahflc package archive --manifest <ahfl.toml> --out <dir>
ahflc package publish [--dry-run] --manifest <ahfl.toml> --registry <id> --out <dir>
ahflc package yank <package>@<version> --registry <id> --reason <text>
ahflc registry resolve --manifest <ahfl.toml> --lockfile <ahfl.lock>
```

使用 `init` 创建 manifest，使用 `archive` 打包源码，使用 `public-api-diff` 检查公开接口变化。发布和解析依赖需要自行配置 registry（包仓库）服务；AHFL 当前不提供官方托管服务。完整配置和发布步骤见 [CLI 命令参考](../reference/cli-commands.zh.md)。

## Observability 与性能诊断

CLI-level telemetry 不等于 workflow runtime evidence：
```bash
"$AHFLC" emit summary -O --time-passes examples/refund/audit.ahfl
"$AHFLC" emit smv --smv-size-report examples/refund/audit.ahfl
"$AHFLC" emit summary \
  --trace-export trace.jsonl \
  --metrics-export metrics.jsonl \
  --structured-log ahflc.jsonl \
  --memory-report memory.json \
  examples/refund/audit.ahfl
```

- `--time-passes` 必须与 `-O` 一起使用。
- `--smv-size-report` 只适用于 `emit smv`。
- trace/metrics/structured-log 是**CLI command** 级 JSONL。
- `--memory-report` 包含源码、类型结果和 IR 的结构估算及平台 RSS（进程驻留内存）；估算值不是精确的分配器用量。
- runtime token、cost、cache、fallback、retry、diagnostic 和 terminal lifecycle 应读取
  `run --output-format json` / `jsonl` 的 canonical facts。
## 交互探索与调试

| 工具 | 用途 | 使用限制 |
|---|---|---|
| `ahfl-repl` | `:type`、`:verify`、`:simulate` 的本地探索 | 不替代 package-aware runtime 调试 |
| `ahfl-dap` | 设置状态断点、步进、查看输入与节点结果 | 不支持函数断点或进入外部服务；见 [调试指南](./debugging.zh.md) |

增量分析在编译器和语言服务器中自动工作，无需启动独立工具。缓存设置与限制见
[增量编译指南](./incremental-compilation.zh.md)。

## 诊断顺序与退出码

遇到问题时从低层到高层排查：
1. `check`：先修 parse / resolve / typecheck / validate diagnostics。
2. `dump ast` 与 `dump types`：确认源码被如何解释。
3. `dump package-graph`：确认 manifest、workspace、dependency 与 sysroot。
4. `emit summary` 与 `emit execution-plan`：确认编译后控制面与 DAG。
5. `emit dry-run-trace`：确认 mock 演练路径。
6. `run --output-format jsonl`：定位真实 runtime / provider / schema 边界。
7. `validate`、`check --verify=on`：分别检查 assurance 和 formal 控制性质。
| 退出码 | 含义 |
|---|---|
| `0` | 成功 |
| `1` | 编译、验证、runtime 或外部 checker 失败 |
| `2` | 命令行参数或输入组合非法 |
| `3` | 内部错误 |

CI 中把默认控制 `check`、按需数据目标 `check --verify-data-contracts`、关键 `emit`、dry run、`run` 和 `validate` 分成独立步骤；
不要用一个大脚本吞掉 source range、checker status 或 JSONL terminal event。
