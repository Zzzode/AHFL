# AHFL 用户教程

从入门教程开始，学习构建工具链、编写和运行 workflow；已有具体任务时，可直接查阅对应指南。

## 推荐阅读顺序

1. [入门教程](./getting-started.zh.md)：构建工具链，运行示例，体验验证、LLM 调用与持久化恢复。
2. [用户指南总览](./overview.zh.md)：了解适用场景、能力边界与完整工作流。
3. [建模指南](./authoring.zh.md)：编写数据、capability、Agent、合同和 workflow。
4. [CLI 工作流](./cli.zh.md)：选择命令、输入方式和诊断步骤。
5. [执行与包指南](./execution.zh.md)：配置 provider、运行 workflow、读取事件并恢复执行。
6. [行为约束与验证](./assurance.zh.md)：检查外部调用声明、控制模型与数据规则。

## 按任务查阅

| 任务 | 教程 |
| --- | --- |
| 配置 package、workspace 和 sysroot | [工程使用](./packages.zh.md) |
| 按需证明数据规则 | [行为约束与验证](./assurance.zh.md)、[退款金额规则示例](../../examples/refund-rules/README.md) |
| 使用编译期 handoff 产物 | [Native handoff 使用](./native-handoff.zh.md) |
| 调试 workflow | [调试指南](./debugging.zh.md) |
| 理解增量编译和持久缓存 | [增量编译指南](./incremental-compilation.zh.md) |
| 使用标准库解决具体问题 | [标准库示例](./standard-library.zh.md) |

## 配套参考

完整参数见 [CLI 命令参考](../reference/cli-commands.zh.md)，诊断编号见 [错误码参考](../reference/error-codes.zh.md)。语法和静态语义以 [语言规范](../spec/core-language.zh.md) 为准，其他文档见 [文档总目录](../README.md)。
