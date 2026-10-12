# AHFL 单文件模式参考

没有 `ahfl.toml` 时，可以检查和格式化独立 `.ahfl` 文件，但不能导入其他模块。需要标准库或多文件工程时，用 `ahflc init --single-file` 创建 package。语言规则见[核心语言规范](../spec/core-language.zh.md)。

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

## 单文件模式的能力与限制

没有 manifest 的文件进入 `DetachedSourceUnit` 模式。该模式允许：

1. 解析和格式化当前文件。
2. 使用当前文件内声明的局部类型和符号。
3. 使用 language primitive prelude。
4. 在编辑器中跳转到配置的标准库里对应内建类型的定义。

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
它会在文件所在目录创建 `ahfl.toml`：

1. 新文件路径会创建 starter source，并写入 `module <prefix>::<file-stem>;`。
2. 已存在但没有 module 声明的 source 会在文件头部补入 module 声明。
3. 已存在 module 声明的 source 会沿用声明中的 module prefix 和导出 module path。
4. 已有 `ahfl.toml` 时命令失败，不覆盖现有 package identity。
5. 生成的 manifest 默认声明 `std = { source = "sysroot" }` dependency；源码仍必须显式
   `import std::...` 才能使用 std module 或 primitive facade impl method。

## LSP 行为

LSP 打开 detached 文件时会发布 `N::detached_source_unit` information diagnostic。
在 primitive token 上：

1. “Go to Definition” 与 “Go to Type Definition” 跳到配置的标准库中的定义位置，例如 `String` 到 `std/string.ahfl`。
2. “Go to Implementations” 不列出标准库对内建类型提供的实现。
3. 若标准库缺少对应类型的源文件，或没有可用 sysroot，编辑器显示 `W::primitive_home_unavailable` warning。

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
