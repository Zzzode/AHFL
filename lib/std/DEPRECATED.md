# /lib/std/ Deprecated

**Status**: DEPRECATED — will be removed after M2 (Wave-24+, approx. 2026-09-15).
**Replacement directory**: `/std/` (flat 13-file layout, 2746 lines, fully maintained).
**Decision ID**: D1 — default value adopted per `docs/plans/corelib-support-workplan.zh.md §4`.
**Approved as default**: 2026-06-29 — "pending review; raise concerns before 2026-07-01".

## Why / 背景

AHFL stdlib 存在两套目录：
- `/std/`：真实实现（12 files + 2746 LOC），包含完整 inherent impl 方法（`opt.map(f)`）。
- `/lib/std/`：早期设计的 Rust 风格 per-module `mod.ahfl` 布局（5 files + 482 LOC），所有 API 都是**命名空间前缀写法**（`option_map(opt, f)`），**没有任何新算法**。

2026-06-29 符号级 diff 确认：`/lib/std/` 独有的 22 个函数名（`option_is_some / list_get / map_contains_key` 等）**全部能在 `/std/` 中找到功能等价的 impl 方法**，功能严格子集。没有新算法，没有新 trait，没有新的语义行为。

## Migration guide / 迁移指南

把 import 路径从 **`lib::std::option` → `std::option`**，并把调用改写为方法调用风格：

| lib/std 旧写法（废弃） | /std/ 新写法（推荐） |
|---|---|
| `option_is_some(opt)` | `opt.is_some()` |
| `option_is_none(opt)` | `opt.is_none()` |
| `option_map(opt, f)` | `opt.map(f)` |
| `option_and_then(opt, f)` | `opt.and_then(f)` |
| `option_or(opt, other)` | `opt.or(other)`（M1-2 补完，先用 `opt.or_else(\|| other|)`） |
| `option_unwrap(opt)` | `opt.unwrap(sentinel)` |
| `option_unwrap_or(opt, d)` | `opt.unwrap_or(d)` |
| `list_length(xs)` | `xs.length()` |
| `list_is_empty(xs)` | `xs.is_empty()` |
| `list_get(xs, i)` | `xs.get(i)` |
| `list_first(xs)` | `xs.first()` |
| `list_last(xs)` | `xs.last()` |
| `list_set(xs, i, x)` | `xs.insert(i, x)`（语义略不同，先用 `Option<List<T>>` 调整） |
| `set_length(s)` / `set_is_empty(s)` / `set_contains(s, x)` | `s.length()` / `s.is_empty()` / `s.contains(x)` |
| `map_length(m)` / `map_is_empty(m)` / `map_contains_key(m, k)` / `map_get(m, k)` | `m.length()` / `m.is_empty()` / `m.contains_key(k)` / `m.get(k)` |

## Fallback behavior / 编译期告警

`src/compiler/syntax/frontend/project.cpp` 中，stdlib 搜索回退路径按以下顺序：
1. `AHFL_STDLIB_SEARCH_ROOT` env（最高优先级）
2. `CMAKE_INSTALL_DATADIR/ahfl`（SDK 安装场景）
3. `AHFL_SOURCE_DIR`（源码开发场景，**会命中 `/std/`，不会命中 `/lib/std/`**）
4. cwd 向上爬目录（检测到 `std/prelude.ahfl` 停止）

> **注意**：`/lib/std/` 不在第 3/4 步中被主动命中。旧代码若仍写 `import lib::std::option;` 会被 resolver 在第 4 步偶然命中（用户源码目录与 `/lib/std/` 同仓时），但会在 `include_stdlib=true`（默认）场景下打印 `DEPRECATION_WARNING`：`importing from /lib/std/ is deprecated; use std::option instead`（M0-2 中 FRONTEND 实装）。

