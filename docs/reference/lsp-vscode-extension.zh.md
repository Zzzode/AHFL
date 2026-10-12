# 在 VS Code 中使用 AHFL

AHFL 扩展提供语法高亮、补全、诊断、格式化和代码导航。语言服务由 `ahfl-lsp` 提供；LSP（Language Server Protocol）是编辑器与语言服务之间的通信协议。

## 安装扩展

使用与操作系统和处理器架构匹配的 VSIX 安装包，例如 `darwin-arm64` 或 `linux-x64`。平台安装包包含 `ahfl-lsp` 和标准库，无需另外配置语言服务路径。

在 VS Code 的 Extensions 视图菜单中选择 **Install from VSIX…**，或运行：

```bash
code --install-extension path/to/ahfl-language-<version>-<target>.vsix
```

然后打开工程所在文件夹和一个 `.ahfl` 文件。编辑器右下角的语言模式应显示 AHFL；悬停类型或输入标识符时应出现类型信息和补全。

如果安装包只包含客户端，必须另外安装 `ahfl-lsp` 并将其放入 PATH，或配置下节中的 `ahfl.serverPath`。

## 选择语言服务和标准库

常用设置可写入工程的 `.vscode/settings.json`：

```json
{
  "ahfl.serverPath": "/path/to/ahfl-lsp",
  "ahfl.toolchain.sysroot": "/path/to/ahfl-toolchain"
}
```

| 设置 | 用途 |
| --- | --- |
| `ahfl.serverPath` | 指定语言服务可执行文件。留空时依次使用扩展内置服务和 PATH 中的 `ahfl-lsp`。 |
| `ahfl.serverArgs` | 传给语言服务的附加参数，默认为空数组。 |
| `ahfl.toolchain.sysroot` | 标准库工具链根目录，须包含 `std/ahfl.toml`；也可直接填该 manifest 的路径。留空时使用平台安装包附带的标准库。 |
| `ahfl.hover.detailLevel` | 悬停信息详略：`compact`、`standard`（默认）、`debug`。 |
| `ahfl.hover.showSource` | 是否在 debug 悬停中显示源文件路径，默认 `false`。 |
| `ahfl.hover.maxFacts` | standard 悬停显示的主要事实数量上限，默认 `3`。 |
| `ahfl.hover.markupKind` | 悬停格式：`auto`（默认）、`markdown`、`plaintext`。 |
| `ahfl-language-server.trace.server` | 通信日志：`off`（默认）、`messages`、`verbose`。 |

`ahfl.toolchain.sysroot` 按文件所属 workspace folder 生效。路径支持 `${workspaceFolder}` 和相对路径；如果工作区本身包含 `std/ahfl.toml`，可以设置：

```json
{
  "ahfl.toolchain.sysroot": "${workspaceFolder}"
}
```

选择 sysroot 只决定标准库的位置。使用标准库模块仍需在 `ahfl.toml` 的 `[dependencies]` 中声明 `std = { source = "sysroot" }`，并在源码中显式 `import`；见[包指南](../tutorials/packages.zh.md)。

## 编辑和导航

打开 `.ahfl` 文件后，可使用 VS Code 的常规功能：

- **Problems**：查看错误和警告，点击诊断跳到相应源码。
- **Hover**：查看符号类型和相关信息。
- **Go to Definition / Type Definition**：跳到声明；`String`、`Int` 等内建类型跳到标准库对应定义位置。
- **Go to Implementations**：查看类型或 trait 的实现，包括标准库提供的方法。
- **Find All References** 或 **AHFL: Show References**：查找引用。
- **Rename Symbol**：重命名符号及其引用。
- **Format Document / Format Selection**：格式化文件或选区。扩展默认启用 AHFL 文件保存时格式化。
- **Go to Symbol in Workspace**：查找工作区符号。

需要查看实现列表时使用 **Go to Implementations**。Cmd-click 或 **Go to Definition** 返回声明位置。

导航能找到某个标准库声明，并不意味着当前源码可以直接调用它；调用仍遵循 import 和可见性规则。

## 打开独立文件

当文件在工作区边界内找不到 `ahfl.toml` 时，编辑器使用单文件模式（`DetachedSourceUnit`）：

1. Problems 中显示 `N::detached_source_unit` 提示。
2. 可以使用内建类型和当前文件声明的符号，也可以格式化文件。
3. 不允许 import，也不提供自动导入、import 补全或 organize imports。
4. 内建类型的定义跳转依赖配置的 sysroot；缺失时显示 `W::primitive_home_unavailable`。
5. 单文件模式不列出标准库对内建类型提供的实现。

需要标准库或多文件模块时，在文件所在目录运行：

```bash
ahflc init --single-file path/to/file.ahfl
```

然后显式导入需要的模块。完整规则见[单文件模式参考](./single-file-mode.zh.md)。

## 排查语言服务问题

在命令面板中运行 **AHFL: Show Language Server Output** 查看服务输出；需要重启时运行 **AHFL: Restart Language Server**。AHFL 文件中也可使用 `Cmd+Shift+L`（macOS）或 `Ctrl+Shift+L`。

| 现象 | 检查与处理 |
| --- | --- |
| 文件只有高亮，没有补全或诊断 | 检查 Output 中的启动错误，确认安装包平台匹配；使用外部服务时检查 `ahfl.serverPath` 或 PATH。 |
| 标准库类型或方法找不到 | 检查 sysroot 下的 `std/ahfl.toml`，以及工程的 std dependency 和源码 import。 |
| 显示 detached 提示，无法 import | 确认文件属于带 `ahfl.toml` 的工程，或先初始化 package。 |
| 内建类型不能跳转 | 检查 `ahfl.toolchain.sysroot` 与标准库源文件，按 `W::primitive_home_unavailable` 提示处理。 |
| 修改配置后服务没有恢复 | 重启语言服务并查看 Output 中的错误。 |

需要记录通信过程时，将 `ahfl-language-server.trace.server` 设置为 `messages` 或 `verbose`，重现问题并查看语言服务输出。报告问题时附上扩展版本、操作系统、相关设置、诊断和最小源码示例。

workflow 执行与调试方式见[调试指南](../tutorials/debugging.zh.md)。
