# AHFL CLI Commands

本文列出 `ahflc` 的命令、选项、输出格式和退出码。选择任务后，先确定输入是 package、workspace member 还是独立 `.ahfl` 文件。

- 检查、格式化和构建工程：见「总览」「输入选项」与「Formatter」。
- 运行 workflow 并读取结果：见「Runtime Output Contract」与[执行指南](../tutorials/execution.zh.md)。
- 配置导出目标：见 [Native handoff 指南](../tutorials/native-handoff.zh.md)。
- 管理依赖和发布包：见「Registry Dependency Resolution」「Package Publish」与[包指南](../tutorials/packages.zh.md)。
- 读取三层 IR 的输出：AHFL-IR 用于验证/编排检查，Core-IR 用于执行层检查，Wasm 是可执行产物。格式见 [IR 参考](./ir-format.zh.md)。

## 总览

项目输入模式：

```text
<input-mode> ::=
    --manifest <ahfl.toml> [--target <name>]
  | --workspace <ahfl.workspace.toml> --package <name> [--target <name>]
  | <input.ahfl>
```

核心命令：

```text
ahflc check <input-mode>
ahflc run --workflow <name> --input '<json>' [--llm-config <path>] [--tool-catalog <path>] <input-mode>
ahflc fmt [--check] <input.ahfl|dir>...
ahflc fmt [--check] --manifest <ahfl.toml>
ahflc fmt [--check] --workspace <ahfl.workspace.toml> --package <name>
ahflc build [--output <path>] [--wasm-profile <wasi|browser>] <input-mode>
ahflc init --single-file <input.ahfl>
ahflc package archive --manifest <ahfl.toml> --out <dir>
ahflc package publish [--dry-run] --manifest <ahfl.toml> --registry <id> --out <dir> [--sysroot <path>] [--semver-gate --from <previous>]
ahflc package yank <package>@<version> --registry <id> [--reason <text>]
ahflc registry resolve --manifest <ahfl.toml> --lockfile <ahfl.lock> [--sysroot <path>]
ahflc dump-ast <input-mode>
ahflc dump-types <input-mode>
ahflc dump package-graph --manifest <ahfl.toml>
ahflc dump package-graph --workspace <ahfl.workspace.toml> --package <name>
ahflc emit-ir <input-mode>
ahflc emit-ir-json <input-mode>
ahflc emit-core-ir-json <input-mode>
ahflc emit-opt-ir <input-mode>
ahflc emit-opt-ir-json <input-mode>
ahflc emit-native-json --manifest <ahfl.toml> --target <handoff-target>
ahflc emit-execution-plan <input-mode>
ahflc emit-dry-run-trace --capability-mocks ... --input-fixture ... [--workflow ...] [--run-id ...] <input-mode>
ahflc emit-package-review <input-mode>
ahflc emit-public-api --manifest <ahfl.toml>
ahflc emit-public-api-docs --manifest <ahfl.toml>
ahflc emit-public-api-diff <old-public-api.json> <new-public-api.json>
ahflc emit-summary <input-mode>
ahflc emit-smv [--smv-size-report] <input-mode>
ahflc emit-smt <input-mode>
ahflc emit-assurance-json <input-mode>
ahflc validate-assurance <input-mode>
```

开发者工具入口：

```text
ahfl-repl [--help]
ahfl-dap [--help]
```

## 选项

### 输入选项

| 选项 | 参数 | 说明 |
|------|------|------|
| `--manifest` | `<ahfl.toml>` | AHFL package manifest；用于 package-aware check / fmt / emit / dump |
| `--workspace` | `<ahfl.workspace.toml>` | AHFL workspace manifest；必须配合 `--package <name>` 选择 member package |
| `--package` | `<name>` | 与 `--workspace` 配合选择 workspace package |
| `--target` | `<name>` | 选择 manifest target；多 target package 必须显式传入 |
| `--sysroot` | `<path>` | AHFL sysroot；可传包含 `std/ahfl.toml` 的工具链根，或直接传 `std/ahfl.toml` |
| `--capability-mocks` | `<mocks.json>` | deterministic capability mock 输入；`run` 中作为 LLM function tools source |
| `--tool-catalog` | `<tools.json>` | 仅 `run`：deterministic runtime tool catalog 输入，作为 LLM function tools source |
| `--capability-bindings` | `<bindings.json>` | 仅 `run`：HTTP/gRPC JSON transcoding runtime capability binding 输入 |
| `--input-fixture` | `<fixture>` | package fixture request 选择 |
| `--run-id` | `<id>` | 稳定 run identity |
| `--workflow` | `<canonical>` | 显式选择目标 workflow |
| `--input` | `<json>` | `run` 命令的真实 runtime 输入 JSON |
| `--input-file` | `<path>` | `run` 命令的 runtime 输入 JSON 文件 |
| `--llm-config` | `<path>` | `run` 命令的 LLM Provider 配置，默认 `~/.ahfl/llm_config.json` |
| `--profile` | `<name>` | 选择 `ahfl.toml` 中的 `[run.profiles.<name>]` |
| `--recovery-store` | `<path>` | durable resume([RFC 0022](../rfcs/0022-durable-capability-resume.zh.md)):存在则本次 `run` 从该快照恢复;能力返回 `PENDING` 挂起时把 resume record 写到该路径。完成的 run 不写快照。 |
| `--resume-pending-result` | `<json>` | 恢复挂起 workflow 时,为那个 pending 能力注入的结果 JSON。非法 JSON 报错并中止。 |
| `--suspend-capability` | `<name>` | 令指定能力在**全新** run 时返回 `PENDING`(挂起),用于从 shell 驱动 suspend→resume 往返;从 `--recovery-store` 恢复时该标志无效(该调用从 memo 命中)。 |
| `--intent-log` | `<path>` | 写入去重:在每次 `durable_write`/`financial_write` 能力派发**前**向该路径追加一行 JSONL write-ahead intent `{idempotency_key, capability}`,供宿主跨崩溃去重。恢复时命中 memo 的调用不重放派发,故不重复记录。 |
| `--output-format` | `<human\|json\|jsonl\|quiet>` | 选择同一 canonical event/report 的输出 renderer |
| `--verbosity` | `<normal\|verbose\|trace>` | 控制展示细节，不改变 event 语义 |

### 输出控制选项

| 选项 | 参数 | 说明 |
|------|------|------|
| `--verify` | `<off\|on>` | `check`/`run`/`build` 默认 `on`：通用 Agent/workflow 控制义务与用户控制时序目标。其他命令默认 `off`。所请求检查 fail closed；不隐式请求数据合同证明 |
| `--verify-data-contracts` | - | 需 `--verify=on`：显式请求 package 数据合同蕴含验证。unsupported、unknown、缺 solver、矛盾前提、反例均失败；证明范围为 `contract_implication` |
| `--formal-backend` | `<nuxmv\|nusmv\|spin\|tlaplus>` | 需 `--verify=on`: 选择形式化 backend。当前只有 `nuxmv` / `nusmv` 支持 AHFL SMV 外部验证；`spin` / `tlaplus` 会报告 `verification_unsupported` |
| `--model-checker` | `<path>` | 需 `--verify=on`: NuSMV/nuXmv checker 路径 |
| `--checker-timeout-seconds` | `<seconds>` | 需 `--verify=on`: 外部 checker 进程 timeout；超时会报告 `checker_status: checker_error` 与 `checker_timed_out: true` |
| `--formal-model-out` | `<model.smv>` | 需 `--verify=on`: 保留 SMV 模型文件 |
| `--output` | `<path>` | 仅 `build`: WASM 二进制输出路径。默认 `<stem>.wasm`(与输入同目录);无法推导时报错 |
| `--wasm-profile` | `<wasi\|browser>` | 仅 `build`: WASM 部署 profile。`wasi`(默认)面向命令行 wasm runtime;`browser` 面向 JS host(capability 走 fetch/JS 代理),无 WASI——文件系统/环境类 capability 在 build 期被确定拒绝。模块 ABI 与 profile 无关 |
| `--dump-ast` | - | 输出 AST outline |
| `--dump-types` | - | 输出类型环境 |
| `--explain` | - | 启用结构化解释 |
| `--check` | - | 仅 `fmt`: 检查格式化状态，不写回文件 |
| `-O` / `--optimize` | - | 启用优化 passes；普通 backend 路径运行 AHFL-IR passes，`emit opt-ir` / `emit opt-ir-json` 输出 Opt IR passes 后的结果 |
| `--time-passes` | - | 与 `-O` 配合使用，把 AHFL-IR pass pipeline 耗时输出到 stderr |
| `--smv-size-report` | - | 仅 `emit smv`: 把 SMV artifact 的 byte、line、LTLSPEC 数量输出到 stderr |
| `--trace-export` | `<trace.jsonl>` | 将 CLI command span 以 JSONL 写入文件，不改变 stdout artifact |
| `--metrics-export` | `<metrics.jsonl>` | 将 CLI duration / exit_code metrics 以 JSONL 写入文件，不改变 stdout artifact |
| `--structured-log` | `<log.jsonl>` | 将 CLI command completion 结构化日志以 JSONL 写入文件，不改变 stdout artifact |
| `--memory-report` | `<memory.json>` | 将 source、TypedProgram、IR 规模和结构性 memory proxy 写入 JSON 文件，不改变 stdout artifact |
| `--out` | `<dir>` | 仅 `package archive` / `package publish`: 输出 package archive 或 publish evidence artifact 的目录 |
| `--registry` | `<id>` | 仅 `package publish` / `package yank`: 发布或 yank 目标 registry id；dry-run 也必须显式传入 |
| `--dry-run` | - | 仅 `package publish`: 执行本地发布门禁并跳过 registry upload |
| `--reason` | `<text>` | 仅 `package yank`: 记录 yank 原因 |
| `--semver-gate` | - | `emit public-api-diff` / `package publish`: 执行 public API SemVer gate |
| `--from` | `<version>` | SemVer gate 的 previous version；`package publish` 用它定位 previous release |
| `--to` | `<version>` | 仅 `emit public-api-diff --semver-gate`: 显式 next version；`package publish` 使用 manifest version |

## 单文件 Check

`ahflc check path/to/file.ahfl` 若在工作区边界内找不到 `ahfl.toml`，不会把当前目录
当作隐式 package root，而是进入 `DetachedSourceUnit`：

1. primitive-only 文件输出 `N::detached_source_unit` note；只要没有 error，退出码为 0。
2. 任意 `import` declaration 输出 `E::detached_import`，退出码为 1。
3. 非 primitive、非当前文件声明的 nominal type 输出
   `E::detached_unknown_nominal_type`。
4. `std::*` module、`List`、`Map`、`Option`、`Result` 和 primitive facade methods
   都需要 manifest、`std = { source = "sysroot" }` dependency 与显式 import。

如果裸文件需要升级为正式工程，使用：

```bash
ahflc init --single-file path/to/file.ahfl
```

该命令只创建文件所在目录的 `ahfl.toml`，并在缺少 module 声明的已有源文件头部补入
`module <prefix>::<file-stem>;`。生成的 manifest 声明标准库依赖：
`[dependencies] std = { source = "sysroot" }` 只建立 package dependency，不会隐式 import
任何 std module；源码仍必须显式写 `import std::...`。

详见 [single-file-mode.zh.md](./single-file-mode.zh.md)。

## Package Archive

`ahflc package archive --manifest <ahfl.toml> --out <dir>` 生成规范化的源码归档。当前命令只接受显式 package manifest，不接受 workspace selector、`--target`、`--sysroot` 或 positional source file；registry upload/yank 由 `package publish` / `package yank` 承担，不混入 archive 命令。

输出文件：

```text
<package>-<version>.source-archive.json
<package>-<version>.source-archive.payload
```

metadata manifest 使用 `ahfl.source_archive` schema，记录 package name、`manifest_sha256`、`archive_sha256` 和每个源码文件的 path/size/digest。payload 是 deterministic binary archive payload，当前只包含 `.ahfl` / `.toml` source inputs，排除 VCS/build/cache/lockfile artifacts，拒绝 symlink，并把 CRLF/CR 归一化为 LF。

示例：

```bash
ahflc package archive --manifest path/to/ahfl.toml --out dist/
```

## Registry Dependency Resolution

manifest package 可以声明 registry dependency。显式解析入口是：

```bash
ahflc registry resolve --manifest path/to/ahfl.toml --lockfile path/to/ahfl.lock --sysroot .
```

该命令会解析 root/transitive registry dependencies，并把选中版本、registry id、source archive digest、manifest digest 和 public API digest 写入 `ahfl.lock`。输出文件名必须是 canonical `ahfl.lock`，这样后续 package-aware 命令可以执行 lockfile drift 检查。

manifest-mode PackageGraph 构建也会执行同一套解析逻辑，例如：

```bash
ahflc dump package-graph --manifest path/to/ahfl.toml --sysroot .
```

如果 root manifest 含 registry dependency，CLI 会：

1. 读取 registry package index，按 exact/caret/tilde requirement 选择最高可用版本。
2. 拉取选中版本的 `ahfl.registry.index`、`ahfl.source_archive` metadata 和 source archive payload。
3. 校验 registry metadata、source archive digest、manifest digest 和 dependency metadata。
4. materialize verified source archive，并把 registry package 作为结构化 PackageGraph input 注入。
5. 递归解析 transitive registry dependencies。

环境变量：

| 变量 | 说明 |
|------|------|
| `AHFL_REGISTRY_URL` | 覆盖默认 registry endpoint；仅默认 `https://registry.ahfl.io` 构造路径读取该变量。 |
| `AHFL_PACKAGE_CACHE` | 指定 registry fetch cache 目录；transport unavailable 时允许回退缓存，live invalid response 不回退。 |

边界：

1. LSP 不会隐式执行 registry 网络拉取。
2. workspace 输入不支持 registry dependency 网络拉取；请用对应 package 的 `--manifest` 解析依赖。
3. registry upload 和 yank 走显式 `package publish` / `package yank`，LSP 与普通 PackageGraph 查询不会隐式执行 mutation。

## Package Publish

`ahflc package publish [--dry-run] --manifest <ahfl.toml> --registry <id> --out <dir> [--sysroot <path>] [--semver-gate --from <previous>]` 检查并发布 package。它会构建 PackageGraph、检查已有 `ahfl.lock` drift、解析并 typecheck exported public API 输入、生成 public API snapshot 和 digest、构建 normalized source archive、拒绝 path/workspace local dependency 泄漏到 registry metadata，并输出 registry index metadata 与 publish evidence。

如果传入 `--semver-gate --from <previous>`，CLI 会读取 registry package index，定位同 registry id 下的 previous release，拉取并校验 previous public API snapshot，然后把 previous snapshot 和当前 snapshot 交给 `public-api-diff --semver-gate` 同一套结构化 SemVer gate。gate 失败时命令失败，不执行 upload；gate 通过时 evidence 记录 `semver_gate: "pass"`、`semver_from`、`semver_to`、`previous_public_api_sha256` 和 previous snapshot artifact。

未传 `--dry-run` 时，CLI 会在本地 gate 全部通过后向 registry 发送 `ahfl.registry.publish_request` envelope。请求包含 `ahfl.registry.index`、`ahfl.source_archive` metadata、source archive payload 和 public API snapshot。客户端只接受同 coordinate、同 digest、同 dependency metadata 且 `yanked=false` 的 registry index 响应，成功后写出 `ahfl.publish` evidence，并设置 `upload_performed: true`。

输出文件：

```text
<package>-<version>.source-archive.json
<package>-<version>.source-archive.payload
<package>-<version>.public-api.json
<package>-<previous>.previous-public-api.json    # only when --semver-gate is used
<package>-<version>.registry-index.json
<package>-<version>.publish-dry-run.json       # with --dry-run
<package>-<version>.publish.json               # without --dry-run
```

`registry-index.json` 使用 `ahfl.registry.index` schema。`publish-dry-run.json` 使用 `ahfl.publish_dry_run` schema，固定记录 `upload_performed: false`；`publish.json` 使用 `ahfl.publish` schema，记录 `upload_performed: true`。未传 `--semver-gate` 时 `semver_gate` 为 `not-run`，传入并通过时为 `pass`。

示例：

```bash
ahflc package publish --dry-run --manifest path/to/ahfl.toml --registry default --out dist/

ahflc package publish --dry-run \
  --manifest path/to/ahfl.toml \
  --registry default \
  --sysroot . \
  --out dist/ \
  --semver-gate --from 1.2.3

ahflc package publish \
  --manifest path/to/ahfl.toml \
  --registry default \
  --sysroot . \
  --out dist/ \
  --semver-gate --from 1.2.3
```

## Package Yank

`ahflc package yank <package>@<version> --registry <id> [--reason <text>]` 标记 registry 中已发布版本为 yanked。yank 不会修改 source archive、manifest digest 或 public API snapshot；它只允许 registry metadata 的 `yanked` 字段从 `false` 变为 `true`。

客户端发送 `ahfl.registry.yank_request`，并只接受同 registry id、同 package、同 version 且 `yanked=true` 的 `ahfl.registry.index` 响应。registry 返回空 body、错误 coordinate 或 `yanked=false` 都会 fail closed。

示例：

```bash
ahflc package yank release-demo@0.2.0 --registry default --reason "bad release"
```

## Formatter

`ahflc fmt <input.ahfl|dir>...` 使用 formatter library 格式化源文件并原地写回；目录输入会递归收集 `.ahfl` 文件并按路径稳定排序。`ahflc fmt --manifest <ahfl.toml>` 会格式化 package module root 内由 PackageGraph 覆盖的 `.ahfl` source；`ahflc fmt --workspace <ahfl.workspace.toml> --package <name>` 会格式化选中 workspace package 的 source。

`ahflc fmt --check ...` 只检查文件是否已经符合格式，不写回；如果需要改动，命令返回非零退出码。批量模式会继续检查所有可读取文件，并在 stderr 输出 `format failed for X of N input(s)` 作为 partial failure 汇总。

配置文件名为 `.ahfl-format`，CLI 会从输入文件所在目录向上查找。当前支持：

```text
indent_width = 4
use_tabs = false
max_line_length = 100
trailing_newline = true
align_fields = true
sort_imports = true
```

## REPL

`ahfl-repl` 启动 AHFL 交互式 REPL；也支持通过 stdin 管道输入命令，便于 smoke test 或一次性诊断。

```bash
ahfl-repl
printf ':help\n:quit\n' | ahfl-repl
```

当前命令：

| 命令 | 用途 |
|------|------|
| `:type <expr>` | 输出表达式推导类型 |
| `:verify <code>` | 对 agent 声明运行形式化验证路径 |
| `:simulate <code>` | 对输入中的 agent 声明运行结构化状态机 step simulation，按 `initial` 到可达 `final` 的最短路径输出状态迁移 |
| `:help` | 输出帮助 |
| `:quit` | 退出 |
| `<expr>` | 求值表达式或输出声明 IR 摘要 |

## DAP

`ahfl-dap` 通过 Debug Adapter Protocol（DAP，编辑器与调试器的通信协议）提供 workflow 调试。使用编辑器的行断点，在状态转移处暂停和步进，查看当前状态、输入输出和已完成节点结果。启动配置与示例见 [调试指南](../tutorials/debugging.zh.md)。

当前不支持按 capability 名称设置函数断点、条件断点、修改变量或 attach 到运行中的进程。Step Into 与 Step Over 行为相同，不能进入外部服务；Step Out 在不同 Agent 的状态转移处暂停。Context 和 handler 局部变量当前不可见。

## 增量分析

在编辑器中继续编辑同一工程时，LSP 会复用未受影响的分析结果，并重新分析受编辑影响的文件与依赖。CLI 检查仍使用 `ahflc check`。使用方式与缓存配置见[增量编译指南](../tutorials/incremental-compilation.zh.md)。

## Profiling

`--time-passes`、`--smv-size-report`、`--trace-export`、`--metrics-export`、`--structured-log` 和 `--memory-report` 用于分析命令耗时、产物规模和执行日志。它们都不改变 stdout artifact；`--time-passes` 和 `--smv-size-report` 报告写入 stderr，`--trace-export`、`--metrics-export`、`--structured-log` 和 `--memory-report` 写入侧路文件。

```bash
ahflc emit summary -O --time-passes examples/refund/audit.ahfl
ahflc emit smv --smv-size-report examples/refund/audit.ahfl
ahflc emit summary \
  --trace-export trace.jsonl \
  --metrics-export metrics.jsonl \
  --structured-log ahflc.jsonl \
  --memory-report memory.json \
  examples/refund/audit.ahfl
```

当前边界：

1. `--time-passes` 必须与 `-O` / `--optimize` 一起使用。
2. `--smv-size-report` 只适用于 `emit smv`，报告 byte、line、LTLSPEC 数量。
3. `--trace-export` 输出 `ahflc.command` span，包含 command、exit_code 和 duration_ms attributes。
4. `--metrics-export` 输出 `ahfl.cli.duration_ms` 与 `ahfl.cli.exit_code` metrics，包含 command label。
5. `--structured-log` 输出 `ahflc command completed` 事件，包含 level、command、exit_code 和 duration_ms fields。
6. `--memory-report` 输出 AHFL 内存报告格式 JSON，包含 source、TypedProgram、IR 规模和结构性 memory proxy；这不是 RSS / allocator 观测。

## Core-IR 分层 JSON Artifact

`emit core-ir-json` / `emit-core-ir-json` 输出执行层 Core-IR 的 `ahfl.core` JSON。输出包含 `format`、固定 `layer: "core"` 判别符，以及 `types` / `value_types` / `capabilities` / `agents` / `flows` / `workflows` / `instances` 表。

```bash
ahflc emit core-ir-json examples/refund/audit.ahfl
```

选择 `emit ir-json` 读取验证与编排结构，选择 `emit core-ir-json` 读取执行结构。Core-IR JSON 是 artifact-only 检查产物，不改变程序或 backend 输入；它只携带逻辑身份，不包含目标平台的物理数据布局。格式与分层边界见 [IR 参考](./ir-format.zh.md#分层投影边界)。

## Workflow Canvas 可视化

`ahflc visualize` 从 execution-plan JSON 生成交互式 HTML canvas（卡片 + 贝塞尔连接线），支持可选的 run-event JSONL trace 叠加执行状态。

```bash
# 静态 canvas（无执行状态）
ahflc visualize plan.json -o canvas.html

# 执行回放（叠加状态、耗时、token）
ahflc visualize plan.json trace.jsonl -o canvas.html

# 静态图导出（文档用）
ahflc visualize plan.json --format dot -o graph.dot
ahflc visualize plan.json --format mermaid -o graph.mmd
```

| 选项 | 用途 |
|------|------|
| `-o <file>` | 输出路径（默认 `trace.html` / `workflow.dot` / `workflow.mmd`，`-` 为 stdout） |
| `--title <text>` | 页面标题 |
| `--format html\|dot\|mermaid` | 输出格式（默认 `html`） |

HTML canvas 功能：

- 自动布局 workflow 节点和依赖连线
- 拖拽平移、滚轮缩放、Fit 自适应
- 点击节点查看详情（agent 类型、输入输出类型、capability、依赖）
- 加载 trace 后：状态徽章（pending/scheduled/running/completed/failed/skipped）、耗时显示、流动虚线动画、时间线、agent 状态机路径

trace 文件由 `ahflc run --output-format jsonl` 生成（`ahfl.run-event` JSONL）。

## Opt IR Artifact

`emit opt-ir` / `emit-opt-ir` 是当前 Opt IR 的文本诊断入口；`emit opt-ir-json` / `emit-opt-ir-json` 是同一 Opt IR 模型的机器可读入口，输出 `AHFL_OPT_IR` JSON artifact。输出包含函数、局部变量、基本块、语句、终结指令、源范围，以及无法优化的时序原子 `skipped_temporal` 记录。

示例：

```bash
# 输出未运行 Opt IR passes 的 Opt IR
ahflc emit opt-ir examples/refund/audit.ahfl

# 先运行 AHFL-IR passes，再输出 Opt IR passes 后的结果
ahflc emit opt-ir -O examples/refund/audit.ahfl

# 输出未运行 Opt IR passes 的 Opt IR JSON
ahflc emit opt-ir-json examples/refund/audit.ahfl

# 先运行 AHFL-IR passes，再输出 Opt IR passes 后的 Opt IR JSON
ahflc emit opt-ir-json -O examples/refund/audit.ahfl
```

使用限制：

1. Opt IR 是 artifact-only 诊断输出。读取稳定的验证与编排结构时，使用 `emit ir-json`。
2. `-O` 会优化 AHFL-IR，因此普通导出产物也可能变化；导出 Opt IR 时还会优化其函数与基本块。
3. Opt IR 文本适合人工比较。脚本使用 `AHFL_OPT_IR` JSON，并检查 `skipped_temporal` 中的未优化原因。

## SMT Contract Artifact 与数据谓词验证

`emit smt` 将可编码的数据合同发射为确定性 SMT-LIB artifact，不启动 solver；对子集外条款写明拒绝原因。artifact 的 check-sat 只是断言一致性检查，不能代替逐目标的证明请求。

```bash
# 仅查看编码
ahflc emit smt --manifest examples/refund-rules/ahfl.toml
# 默认控制验证 + 显式数据目标
ahflc check --manifest examples/refund-rules/ahfl.toml --verify-data-contracts
```

默认数据 requires 是环境前提，数据 ensures/invariant/forbid 为 not_requested；不启动 Z3、不发未请求的子集警告。`--verify-data-contracts` 请求现有 package 数据目标，不能与 `--verify=off` 同用。

当前范围为 **contract_implication**：类型边界与 requires 一致性成立后，检查 requires 是否蕴含 goal。invariant 不能被循环用作假设；前提矛盾、unsupported 目标/必要前提、solver_unavailable、unknown、timeout、公式反例均导致非零退出。Z3 发现顺序为 AHFL_Z3_PATH → ahflc 同目录 bundled Z3 → PATH。

SMT 反例给出合同变量赋值，不是 flow 执行轨迹。flow 赋值和 capability 结果没有编码，成功不能表示执行实现满足后置条件。数据目标结果与生成控制义务、用户时序目标/条件证明分开报告；runtime contract checks 当前 not_implemented。

退款审核示例的三条宿主 predicate requires 是环境声明，non_empty ensures 默认未请求；显式请求会因这些无规则定义的 predicate 不支持而失败并定位条款。可证明规则见 [退款规则示例](../../examples/refund-rules/README.md)，规范见 [Assurance Spec](../spec/assurance.zh.md) 与 [RFC 0017](../rfcs/0017-bmc-contract-semantics.zh.md)。

## Public API Artifact

`emit public-api` 和 `emit public-api-docs` 是 package-level artifact。它们读取
`ahfl.toml` / `ahfl.workspace.toml` 的 `[exports].modules`，根据导出的公开声明与别名生成 API 列表。它们不需要 `--target`，也不会从
handoff target metadata 推导 API 面。

```bash
# JSON snapshot，schema 为 ahfl.public_api
ahflc emit public-api \
  --manifest tests/integration/package_graph_manifest/ahfl.toml \
  --sysroot .

# Markdown docs
ahflc emit public-api-docs \
  --workspace tests/integration/check_ok/ahfl.workspace.toml \
  --package lib \
  --sysroot .

# 比较两个 snapshot
ahflc emit public-api-diff old.public-api.json new.public-api.json

# 比较两个 snapshot，并按 SemVer bump 执行兼容性 gate
ahflc emit public-api-diff \
  --semver-gate --from 1.2.3 --to 2.0.0 \
  old.public-api.json new.public-api.json
```

当前边界：

1. Snapshot 只包含选中 root package 的 API-reachable `pub` symbol 和 `pub use` alias；依赖 package 的 public symbol 不会冒充本包 API。
2. Entry identity 使用 `symbol:<namespace>:<canonical>` 或 `alias:<namespace>:<canonical>`；entry 内保留当前编译会话的 `symbol_id` / `alias_id` / `target_symbol_id` 作为结构化语义来源。
3. `signature` 字段携带结构化参数、字段、variant、effect、where 等 facts；`signature_text` 只用于展示和 diff 摘要。
4. `public-api-diff` 只读取两个 `ahfl.public_api` JSON snapshot，不接受 `--manifest`、`--workspace`、`--target` 或 `--sysroot`。
5. `public-api-diff --semver-gate --from <old> --to <new>` 会基于 diff severity 执行 fail-closed SemVer gate：removed/changed API 需要 major bump，added API 需要 minor bump，无 public API diff 至少需要 patch bump；`0.x` breaking diff 在仍保持 `0.x` 时按 minor bump gate。
6. 发布时可用 `package publish --semver-gate --from <old>` 检查版本兼容性；只比较两个已有 snapshot 时使用 `public-api-diff`。

## Runtime Output Contract

真实 workflow 执行统一使用 `ahflc run`。manifest 可以通过 `[run]` 和
`[run.profiles.<name>]` 提供 target、input、LLM config、output format 与 verbosity；
CLI 选项覆盖 profile：

```bash
ahflc run --manifest examples/execution-demo/ahfl.toml
ahflc run --manifest examples/execution-demo/ahfl.toml --profile low-risk
ahflc run --manifest examples/execution-demo/ahfl.toml \
  --output-format jsonl --verbosity trace
```

| Output format | 数据来源 | 合同 |
| --- | --- | --- |
| `human` | `ExecutionReport` | 默认业务摘要、节点状态、关键诊断 |
| `json` | `ExecutionReport` | 当前格式的单对象机器报告 |
| `jsonl` | `ExecutionEventStore` | 按 `ExecutionEventId` 输出完整事件流 |
| `quiet` | terminal status | 只保留退出码与必要错误 |

replay、audit、scheduler、checkpoint 和 recovery 都是同一 event store 的 library
projection。需要接入审查系统时，应消费 JSON/JSONL
输出或直接使用 `include/ahfl/runtime/` 下的 projection API，human output 仅供阅读，不适合作为脚本接口。

## 退出码

| 退出码 | 含义 |
|--------|------|
| `0` | 成功 |
| `1` | 编译/验证错误 (parse / resolve / typecheck / validate / runtime) |
| `2` | 命令行参数非法 |
| `3` | 内部错误 (不变式违反) |

## 结果字段

控制报告使用 `verification_scope: control_model`、`control_status`，并分别列出 `generated_control_proven` / `generated_control_bounded` 与 `user_temporal_proven` / `user_temporal_bounded` / `user_temporal_conditional` / `user_temporal_unsupported`。无目标时报告 no_targets/not_run，不能声称证明通过。

数据报告使用 `data_contract_status`（not_requested、proved、bounded、refuted、inconsistent_assumptions、unknown、unsupported、solver_unavailable、no_targets）、`data_contract_scope: contract_implication`、`data_contract_assumptions` 和 `data_contract_goals`；逐条目标保留来源。`runtime_contract_checks: not_implemented` 表示当前没有自动 runtime 合同检查。
