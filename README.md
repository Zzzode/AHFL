<p align="center">
  <h1 align="center">AHFL</h1>
  <p align="center">
    <strong>Agent 控制平面 DSL — 让 Agent 行为在执行前可审计</strong>
  </p>
  <p align="center">
    <a href="https://github.com/Zzzode/AHFL/actions/workflows/ci.yml"><img src="https://github.com/Zzzode/AHFL/actions/workflows/ci.yml/badge.svg?branch=main" alt="CI"></a>
    <img src="https://img.shields.io/badge/C%2B%2B-23-blue.svg" alt="C++23">
    <img src="https://img.shields.io/badge/CMake-3.22%2B-064F8C.svg" alt="CMake 3.22+">
  </p>
</p>

---

AHFL（Agent Handoff Flow Language）是一门强类型 DSL，用于描述 Agent 状态机、行为契约、流程编排与多 Agent 工作流。配套编译器 `ahflc` 负责解析、类型检查、形式化验证，并输出可被下游工具消费的结构化中间表示。

## Highlights

- **状态机建模** — Agent 以显式状态、转移、能力白名单定义
- **行为契约** — `requires` / `ensures` / `invariant` / `forbid` 表达前后置条件
- **工作流编排** — DAG 拓扑调度 + 安全性/活性时序公式
- **端到端执行** — 本地解释器 + LLM Provider 适配器，支持真实多 Agent 协作
- **形式化后端** — 受限 SMV 输出支持模型检查
- **零运行时依赖** — 纯 C++23 编译器，无外部库依赖

## Language Preview

```ahfl
agent RefundAudit {
    input: RefundRequest;
    output: RefundDecision;
    states: [Init, Auditing, Approved, Rejected, Terminated];
    initial: Init;
    final: [Terminated];
    capabilities: [OrderQuery, AuditDecision];

    transition Init -> Auditing;
    transition Auditing -> Approved;
    transition Auditing -> Rejected;
}

contract for RefundAudit {
    requires: order_exists(input.order_id);
    invariant: always not called(RefundExecute);
}

workflow RefundWorkflow {
    input: RefundRequest;
    output: RefundDecision;
    node audit: RefundAudit(input);
    liveness: eventually completed(audit, Terminated);
    return: audit;
}
```

## Quick Start

### Prerequisites

| Tool | Version |
|------|---------|
| C++ Compiler | C++23 support (GCC 13+, Clang 17+, Apple Clang 15+) |
| CMake | 3.22+ |
| Ninja | recommended |

### Build & Run

```bash
# 配置 & 构建
cmake --preset dev
cmake --build --preset build-dev

# 类型检查
./build/dev/src/cli/ahflc check examples/refund_audit_core_v0_1.ahfl

# 输出 IR
./build/dev/src/cli/ahflc emit-ir-json examples/refund_audit_core_v0_1.ahfl

# 真实 LLM 执行（需要配置 ~/.ahfl/llm_config.json）
./build/dev/src/cli/ahfl-run examples/refund_audit_core_v0_1.ahfl --workflow RefundWorkflow

# 运行测试
ctest --preset test-dev
```

## Architecture

```mermaid
flowchart LR
    subgraph Frontend
        A[AHFL Source] --> B[ANTLR Parser]
        B --> C[AST]
        C --> D[Resolver]
        D --> E[Type Checker]
        E --> F[Validator]
    end
    subgraph Backend
        F --> G[IR Lowering]
        G --> H[JSON / Native Emitter]
        G --> I[SMV Formal Backend]
        G --> J[Runtime Session]
    end
    subgraph Execution
        J --> K[Evaluator]
        K --> L[State Machine Runtime]
        L --> M[Workflow DAG Scheduler]
        M --> N[LLM Provider Bridge]
    end
```

## Project Structure

```
├── grammar/              ANTLR4 grammar definition
├── include/ahfl/         Public compiler headers
├── src/
│   ├── frontend/         Parser, AST, project loading
│   ├── semantics/        Resolver, type checker, validator
│   ├── ir/               Semantic IR model
│   ├── backends/         Emitters (IR, JSON, Native, SMV, ...)
│   ├── evaluator/        Expression & statement interpreter
│   ├── runtime/          Agent/Workflow runtime engine
│   ├── llm_provider/     LLM capability provider (OpenAI-compatible)
│   └── cli/              CLI entry points (ahflc, ahfl-run)
├── tests/                Regression & E2E tests (815+)
├── examples/             Example .ahfl programs
└── docs/                 Specs, designs, plans
```

## Documentation

| Category | Entry Point |
|----------|-------------|
| Language Spec | [`docs/spec/core-language-v0.1.zh.md`](docs/spec/core-language-v0.1.zh.md) |
| CLI Reference | [`docs/reference/cli-commands-v0.10.zh.md`](docs/reference/cli-commands-v0.10.zh.md) |
| IR Format | [`docs/reference/ir-format-v0.3.zh.md`](docs/reference/ir-format-v0.3.zh.md) |
| Project System | [`docs/reference/project-usage-v0.5.zh.md`](docs/reference/project-usage-v0.5.zh.md) |
| Full Doc Index | [`docs/README.md`](docs/README.md) |

## Development

```bash
# 可用 Configure Presets
cmake --preset dev            # Debug + sanitizer-friendly
cmake --preset release        # Release 优化
cmake --preset asan           # AddressSanitizer

# 格式化
cmake --build --preset build-format        # 执行 clang-format
cmake --build --preset build-format-check  # 仅检查

# 测试特定版本切片
ctest --preset test-dev -L ahfl-v0.42

# 重新生成 Parser
ANTLR_JAR=/path/to/antlr-4.x-complete.jar ./scripts/regenerate-parser.sh
```

## Contributing

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feat/my-feature`)
3. 确保 `ctest --preset test-dev` 全部通过
4. 确保 `cmake --build --preset build-format-check` 无违规
5. 提交 Pull Request

详细指南请参阅 [`docs/reference/contributor-guide-v0.42.zh.md`](docs/reference/contributor-guide-v0.42.zh.md)

## Status

AHFL 当前处于 **v0.56** 阶段，已实现：

- 完整的编译器前端（解析 → 语义分析 → IR 生成）
- 100+ CLI 命令覆盖从执行计划到 Provider 生产就绪的完整 artifact 链
- 本地表达式/语句解释器 + 状态机运行时 + 工作流 DAG 调度器
- LLM Provider 适配器（OpenAI-compatible API，支持真实多 Agent 协作）
- 815+ 回归测试

## Roadmap

- [ ] 更多 Provider 适配器（本地文件系统、数据库）
- [ ] WASM 编译目标
- [ ] LSP 语言服务器
- [ ] VS Code 插件
- [ ] 在线 Playground
