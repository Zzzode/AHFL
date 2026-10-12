<p align="center">
  <h1 align="center">AHFL</h1>
  <p align="center">
    <strong>用于可审计 Agent 工作流的强类型 DSL 与 C++23 编译器</strong>
  </p>
  <p align="center">
    <a href="https://github.com/Zzzode/AHFL/actions/workflows/ci.yml"><img src="https://github.com/Zzzode/AHFL/actions/workflows/ci.yml/badge.svg?branch=main" alt="CI"></a>
    <img src="https://img.shields.io/badge/C%2B%2B-23-blue.svg" alt="C++23">
    <img src="https://img.shields.io/badge/License-Apache--2.0-green.svg" alt="Apache-2.0">
  </p>
  <p align="center">
    <a href="README.md">English README</a>
    ·
    <a href="docs/README.md">文档索引</a>
    ·
    <a href="docs/reference/cli-commands.zh.md">CLI 参考</a>
  </p>
</p>

AHFL（Agent Handoff Flow Language）是一门用于建模和执行可审计 Agent 工作流的强类型领域专用语言（DSL），附带 C++23 编译器。

## 你可以用 AHFL 做什么

AHFL 用于需要明确执行顺序、能力调用边界、失败处理、回放和审计的工作流。
目前为 Beta 版本，升级时语言或命令变化可能影响已有项目。

- <!-- beta-capability:BETA-01 schema=ahfl.beta-evidence.run-profiles --> 将工作流输入和选项保存为 manifest 运行配置，再用 `ahflc run --profile` 选择配置。
- <!-- beta-capability:BETA-02 schema=ahfl.beta-evidence.runtime-identity --> 通过执行报告中的数值 ID 关联工作流、节点、Agent、能力、调用、值和事件。
- <!-- beta-capability:BETA-03 schema=ahfl.beta-evidence.event-projections --> 以文本、JSON 或 JSONL 查看同一次执行，并使用执行事件回放和审计。
- <!-- beta-capability:BETA-04 schema=ahfl.beta-evidence.lifecycle-matrix --> 通过终止事件识别运行结果，包括成功、失败、取消和超时。
- <!-- beta-capability:BETA-05 schema=ahfl.beta-evidence.formatter-idempotence --> 格式化 AHFL 源码时保留注释、属性、效果标注、泛型、导入和可见性；重复格式化不会继续改变结果。
- <!-- beta-capability:BETA-06 schema=ahfl.beta-evidence.stdlib-container-migration --> 使用标准库泛型类型 `List`、`Map`、`Set` 和 `Option` 表达集合和可选值。
- <!-- beta-capability:BETA-08 schema=ahfl.beta-evidence.install-smoke --> 安装附带标准库的 CLI 和语言服务器，或安装内置语言服务器和标准库的平台 VSIX 扩展包。
- <!-- beta-capability:BETA-10 schema=ahfl.beta-evidence.product-scope-freeze --> 通过 CLI 参考查找可用命令和输出产物，并用 `ahflc --help` 查看已安装编译器的选项。

建议从[事件分诊示例](examples/execution-demo/README.md)开始：它连接三个 Agent，
通过兼容 OpenAI 的接口调用大语言模型（LLM）。需要声明业务合同和验证时序规则时，
参见[保障指南](docs/tutorials/assurance.zh.md)。

Native Protobuf 传输、多区域运行、官方包注册服务、托管浏览器 Playground 和
Marketplace 分发不在已验证的 Beta 能力范围内。可以在本地试用 Playground，
参见[启动步骤](tools/playground/README.md)。

## 语言预览

摘自 [examples/refund/audit.ahfl](examples/refund/audit.ahfl)：

```ahfl
agent RefundAudit {
    input: RefundRequest;
    context: RefundContext;
    output: RefundDecision;
    states: [Init, Auditing, Approved, Rejected, Terminated];
    initial: Init;
    final: [Terminated];
    capabilities: [OrderQuery, AuditDecision, TicketCreate];

    transition Init -> Auditing;
    transition Auditing -> Approved;
    transition Auditing -> Rejected;
    transition Approved -> Terminated;
    transition Rejected -> Terminated;
}

contract for RefundAudit {
    requires: order_exists(input.order_id);
    ensures: non_empty(output.reason);
    invariant: always not called(RefundExecute);
}

workflow RefundAuditWorkflow {
    input: RefundRequest;
    output: RefundDecision;
    node audit: RefundAudit(input);
    liveness: eventually completed(audit, Terminated);
    return: audit;
}
```

## 快速开始

### 前置条件

| 工具 | 要求 |
| --- | --- |
| C++ 编译器 | 支持 C++23。推荐 GCC 13+、Clang 17+ 或 Apple Clang 15+。 |
| CMake | 3.22+ |
| Ninja | 推荐，仓库 presets 默认使用 Ninja。 |
| NuSMV / Z3 | 随仓库构建安装，分别用于默认控制验证和按需数据验证。 |
| Node.js | 可选，仅 VS Code 扩展开发或打包需要。 |

### 从源码构建

```bash
git clone https://github.com/Zzzode/AHFL.git
cd AHFL

AHFL_BUILD_JOBS=$(getconf _NPROCESSORS_ONLN)
cmake --preset dev
cmake --build --preset build-dev -j"$AHFL_BUILD_JOBS"
```

### 运行编译器

用 `check` 查找类型错误并验证声明的状态机和工作流规则。添加
`--verify-data-contracts` 可以证明数据规则在给定前提下成立；该证明不涵盖
flow 实现是否符合规则，运行时也不会自动检查宿主谓词。参见
[退款规则示例](examples/refund-rules/README.md)。

运行事件分诊工作流前，先按示例完成[模型服务配置](examples/execution-demo/README.md)。
示例从环境变量读取 API 密钥。

```bash
# 检查类型与默认有限控制模型。
./build/dev/src/tooling/cli/ahflc check examples/refund/audit.ahfl

# 按需请求有界退款金额规则的合同蕴含证明。
./build/dev/src/tooling/cli/ahflc check --manifest examples/refund-rules/ahfl.toml --verify-data-contracts

# 通过 manifest 运行配置启动事件分诊工作流。
cd examples/execution-demo
../../build/dev/src/tooling/cli/ahflc run --output-format json

# 查看所有命令和输出产物。
../../build/dev/src/tooling/cli/ahflc --help
```

模型服务选项、输入和执行报告的用法，参见[执行指南](docs/tutorials/execution.zh.md)。

## 文档

| 主题 | 入口 |
| --- | --- |
| 文档索引 | [docs/README.md](docs/README.md) |
| 用户指南 | [docs/tutorials/README.md](docs/tutorials/README.md) |
| 语言规范 | [docs/spec/core-language.zh.md](docs/spec/core-language.zh.md) |
| CLI 参考 | [docs/reference/cli-commands.zh.md](docs/reference/cli-commands.zh.md) |
| IR 格式 | [docs/reference/ir-format.zh.md](docs/reference/ir-format.zh.md) |
| 管理项目与工作区 | [docs/tutorials/packages.zh.md](docs/tutorials/packages.zh.md) |
| 执行事件与恢复 | [docs/reference/native-runtime-artifacts.zh.md](docs/reference/native-runtime-artifacts.zh.md) |
| VS Code LSP 扩展 | [docs/reference/lsp-vscode-extension.zh.md](docs/reference/lsp-vscode-extension.zh.md) |
| 贡献指南 | [docs/reference/contributor-guide.zh.md](docs/reference/contributor-guide.zh.md) |

## 在 VS Code 中编辑工作流

语言服务器通过语言服务器协议（LSP）提供诊断、补全和导航。可以单独构建：

```bash
cmake --build --preset build-dev --target ahfl-lsp -j"$(getconf _NPROCESSORS_ONLN)"
```

生成可安装的 VSIX 扩展包，内置语言服务器与标准库：

```bash
scripts/package-vscode-vsix-release.sh
code --install-extension tools/vscode/dist/ahfl-language-<version>-<target>.vsix
```

打开 `.ahfl` 文件后即可使用诊断、补全和导航。配置与故障排查见
[VS Code 指南](docs/reference/lsp-vscode-extension.zh.md)。

## 参与开发

编译器开发与贡献流程，参见
[贡献指南](docs/reference/contributor-guide.zh.md)。

## License

AHFL 使用 [Apache License 2.0](LICENSE) 许可证。
