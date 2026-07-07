# AHFL 单文件模式参考

本文定义无 `ahfl.toml` 的 `.ahfl` 文件在 CLI 与 LSP 中的用户可见语义。
规范性语言边界见 [core-language.zh.md](../spec/core-language.zh.md)，设计来源见
[RFC 0008](../rfcs/0008-single-file-std-primitive-semantics.zh.md)。

## 名字来源

AHFL 把名字来源分成三类：

| 来源 | 示例 | 是否需要 import | 是否需要 manifest |
|------|------|-----------------|-------------------|
| Language primitive prelude | `Unit`、`Bool`、`Int`、`Float`、`String`、`UUID`、`Timestamp`、`Duration`、`Decimal` | 否 | 否 |
| Std package modules | `std::collections::List`、`std::json::JsonValue`、`std::fmt::format` | 是 | 是 |
| User package modules | `app::audit::Workflow`、workspace dependency module | 是 | 是 |

Primitive 类型是语言内建类型，不从 `std::*` import 获得。`std` 中可以为 primitive
提供 facade impl，例如 `impl String`，但这些方法仍属于 std package module，
不属于 language primitive prelude。

## DetachedSourceUnit

没有 manifest 的文件进入 `DetachedSourceUnit` 模式。该模式允许：

1. 解析和格式化当前文件。
2. 使用当前文件内声明的局部类型和符号。
3. 使用 language primitive prelude。
4. 在 LSP 中通过 active sysroot 跳转 primitive canonical home。

该模式禁止：

1. 自动加载当前目录、workspace sibling 或 active sysroot 下的整个 `std`。
2. 接受任何 `import` declaration。
3. 把 `List`、`Map`、`Option`、`Result` 等 std nominal types 当作裸名可见。
4. 解析、补全或展示 std facade impl methods。
5. 把当前目录当作隐式 package root。

## CLI 行为

`ahflc check path/to/file.ahfl` 若向上找不到 `ahfl.toml`，会按 detached mode
检查单文件：

1. primitive-only 文件返回 0，并输出 `N::detached_source_unit` note。
2. 出现任何 `import` declaration 时返回非零，并输出 `E::detached_import`。
3. 使用非 primitive、非本文件声明的 nominal type 时输出
   `E::detached_unknown_nominal_type`。

`ahflc fmt path/to/file.ahfl` 仍然只做 parse/format，不要求 manifest。

## 初始化为 package

`ahflc init --single-file path/to/file.ahfl` 是 detached 文件升级到正式 package 的显式入口。
它不会让裸文件隐式获得 std；它会在文件所在目录创建 RFC 0005 `ahfl.toml`：

1. 新文件路径会创建 starter source，并写入 `module <prefix>::<file-stem>;`。
2. 已存在但没有 module 声明的 source 会在文件头部补入 module 声明。
3. 已存在 module 声明的 source 会沿用声明中的 module prefix 和导出 module path。
4. 已有 `ahfl.toml` 时命令失败，不覆盖现有 package identity。
5. 生成的 manifest 默认声明 `std = { source = "sysroot" }` dependency；源码仍必须显式
   `import std::...` 才能使用 std module 或 primitive facade impl method。

## LSP 行为

LSP 打开 detached 文件时会发布 `N::detached_source_unit` information diagnostic。
在 primitive token 上：

1. `textDocument/definition` 与 `textDocument/typeDefinition` 使用 active sysroot 的
   `SysrootPrimitiveIndex` 返回 canonical home，例如 `String` 到 `std/string.ahfl`。
2. `textDocument/implementation` 不返回 std facade impl candidates。
3. 若 active sysroot 缺少某个 primitive home，或没有 default ToolchainProfile，
   server 发布 `W::primitive_home_unavailable` warning。

Detached mode 下，completion/code action 不提供 import completion、organize imports
或自动导入 std module 的修复动作。

## 使用 std 的正确方式

用户 package 使用 std module 必须同时满足：

1. 文件属于 `ahfl.toml` package。
2. manifest 声明：

```toml
[dependencies]
std = { source = "sysroot" }
```

3. 当前文件显式 import 需要的 module：

```ahfl
module app::main;

import std::collections as collections;
import std::string;

struct Payload {
    values: collections::List<Int>;
}

fn length_of(s: String) -> Int effect Pure decreases 0 {
    return s.length();
}
```

`String` 类型本身不需要 import；`s.length()` 需要 `std::string` 中的 facade impl
可见。
