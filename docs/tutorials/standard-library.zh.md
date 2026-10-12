# 使用 AHFL 标准库

本指南按常见数据处理任务展示 `std::*` 的用法：处理缺失值与错误、解析字符串、操作集合，以及组合多个步骤。

## 准备使用标准库的工程

在 `ahfl.toml` 中声明标准库依赖：

```toml
[dependencies]
std = { source = "sysroot" }
```

将下面的 import 放在源码顶部，将局部 `let` 片段放在 `fn` 的函数体内；带有完整 `fn` 的示例可直接作为顶层声明。然后检查工程：

```bash
ahflc check --manifest path/to/ahfl.toml --target lib --sysroot /path/to/AHFL
```

将 manifest 路径、target 名和标准库根目录替换为自己的配置。独立源文件不会自动加载标准库；创建工程的方法见 [单文件使用指南](../reference/single-file-mode.zh.md)。

## 按任务选择类型

| 任务 | 使用什么 |
| --- | --- |
| 表示结果可能不存在 | `Option<T>` |
| 区分成功结果与错误 | `Result<T, E>` |
| 查找、拆分或解析文本 | `String` |
| 保留元素顺序 | `List<T>` |
| 对元素去重或做集合运算 | `Set<T>` |
| 按键查找值 | `Map<K, V>` |
| 比较并限制数值范围 | `std::cmp` |

## 处理可缺失值：Option<T>

```ahfl
import std::option as option;

// Construction + inspection
let some_value: option::Option<Int> = option::Option::Some(42);
let none_value: option::Option<Int> = option::Option::None;
let has = some_value.is_some();          // true
let empty = none_value.is_none();        // true

// Transform the inner value
let doubled = some_value.map<Int>(\x: Int -> x * 2);          // Some(84)
let fallback = none_value.unwrap_or(0);                       // 0

// Chain computations that return Option
let n = some_value.and_then<Int>(\x: Int -> option::Option::Some(x + 1));  // Some(43)

// Filter + recover
let kept = some_value.filter(\x: Int -> x > 10);         // Some(42)
let recovered = none_value.or_else(\ -> option::Option::Some(7));  // Some(7)

// Exactly-one-of: xor
let x = option::xor<Int>(some_value, none_value);                   // Some(42)
```

## 处理成功与错误：Result<T, E>

```ahfl
import std::result as result;
import std::option as option;

let ok: result::Result<Int, String> = result::Result::Ok(5);
let err: result::Result<Int, String> = result::Result::Err("boom");

// Branch on outcome
let val = ok.unwrap_or(0);                              // 5
let recovered = err.or_else<String>(\e: String -> result::Result::Ok(0)).unwrap_or(0);  // 0

// Map success / failure
let bumped = ok.map<Int>(\x: Int -> x + 1);             // Ok(6)
let labelled = err.map_err<String>(\e: String -> e);    // Err("boom") -- transform the error

// Convert to Option
let ok_value = ok.ok();                                 // Some(5)
let err_value = err.err();                              // Some("boom")

// Bridge Option <-> Result
let r = result::from_option<Int, String>(option::Option::Some(7), \ -> "missing");  // Ok(7)
```

## 查找与解析字符串

```ahfl
import std::string as string;

// Length / emptiness / search
let n = "hello".length();              // 5
let has = "hello".contains("ell");     // true
let prefix = "hello".starts_with("he"); // true
let suffix = "hello".ends_with("lo");   // true

// Slice + repeat
let part = "hello".substring(1, 4);    // "ell"
let rep = "ab".repeat(3);              // "ababab"

// Trim + replace
let clean = "  hi  ".trim();           // "hi"
let ls = "  hi  ".trim_start();        // "hi  "
let rs = "  hi  ".trim_end();          // "  hi"
let swapped = "a-b-c".replace("-", "+");  // "a+b+c"

// Split -> List<String>
let fields = "a,b,c".split(",");        // ["a","b","c"]
let words = "a b  c".split_whitespace(); // ["a","b","c"]

// Parse -> Option
let parsed = "42".parse_int();          // Some(42)
let fp = "3.14".parse_float();          // Some(3.14)
```

## 操作有序列表：List<T>

```ahfl
import std::collections as collections;
import std::option as option;

let xs = collections::list_cons<Int>(3, collections::list_cons<Int>(1, collections::singleton<Int>(2)));
// xs == [3, 1, 2]

let len = xs.length();                 // 3
let first = xs.first();                // Some(3)
let item = xs.get(1);           // Some(1)

// fold / map / filter
let sum = xs.fold<Int>(0, \(acc: Int, x: Int) -> acc + x);  // 6
let doubled = xs.map<Int>(\x: Int -> x * 2);
let big = xs.filter(\x: Int -> x > 1);

// sort + dedup for a unique sorted set-as-list
let sorted = collections::sort<Int>(xs);                // [1, 2, 3]
let unique = collections::dedup<Int>(sorted);           // [1, 2, 3]

// Concatenate
let both = xs.append(xs);            // length 6
```

## 做集合运算：Set<T>

```ahfl
import std::collections as collections;

let a = collections::set_singleton<Int>(1);
let b = collections::set_singleton<Int>(2);

// Membership
let has_one = a.contains(1);         // true

// Set algebra
let u = collections::set_union<Int>(a, b);              // {1, 2}
let i = collections::set_intersection<Int>(u, b);       // {2}
let d = collections::set_difference<Int>(u, b);         // {1}
let sub = collections::set_is_subset<Int>(a, u);        // true
```

## 按键查找：Map<K, V>

```ahfl
import std::collections as collections;
import std::option as option;

let phone = collections::map_singleton<String, String>("alice", "555");
let lookup = phone.get("alice");  // Some("555")
let has_key = phone.contains_key("alice");  // true
let empty_q = phone.is_empty();     // false
```

## 比较和限制范围：std::cmp

```ahfl
import std::cmp as cmp;

// Three-way compare
let ord = cmp::compare<Int>(1, 2);        // Less
let is_less = ord.is_less();               // true

// Min / max / clamp
let lo = cmp::min<Int>(5, 3);              // 3
let hi = cmp::max<Int>(5, 3);              // 5
let bounded = cmp::clamp<Int>(15, 0, 10);  // 10

// Reverse a comparison result
let rev = cmp::compare<Int>(1, 2).reverse();  // Greater
```

---

## 组合多个数据处理步骤

```ahfl
import std::string as string;
import std::option as option;
import std::result as result;
import std::collections as collections;

// Parse a comma-separated list of ints, sum the valid ones (invalid -> 0).
fn sum_parsed(csv: String) -> Int effect Pure decreases 0 {
    let parts = csv.split(",");
    return parts.fold<Int>(
        0,
        \(acc: Int, s: String) -> acc + s.parse_int().unwrap_or(0)
    );
}
```

链式调用泛型方法时，如果类型无法推断，可显式指定类型参数（例如 `map<Int>`），或把中间结果绑定到带类型标注的 `let`。

## 使用 ? 传播缺失值或错误

`?` 提取 `Option<T>` 或 `Result<T, E>` 的成功值；遇到 `None` 或 `Err` 时，立即从当前函数返回失败值。多个步骤可以依次使用 `?`，后续步骤只会在前一步成功后执行。

### 组合可缺失值

```ahfl
import std::option as option;

fn hex_to_rgb(
    r: option::Option<Int>,
    g: option::Option<Int>,
    b: option::Option<Int>
) -> option::Option<Int> effect Pure decreases 0 {
    let rv: Int = r?;
    let gv: Int = g?;
    let bv: Int = b?;
    return option::Option::Some(rv * 65536 + gv * 256 + bv);
}
```

任一分量为 `None` 时，函数返回 `None`；三个分量都有值时才组合结果。

### 传播第一个错误

```ahfl
import std::result as result;

fn divide(a: Int, b: Int, error: Int) -> result::Result<Int, Int> effect Pure decreases 0 {
    if b == 0 {
        return result::Result::Err(error);
    }
    return result::Result::Ok(a / b);
}

fn div_chain(a: Int, b: Int, c: Int) -> result::Result<Int, Int> effect Pure decreases 0 {
    let q1: Int = divide(a, b, 1)?;
    let q2: Int = divide(q1, c, 2)?;
    return result::Result::Ok(q2);
}
```

第一次除法失败时返回错误码 `1`，第二次失败时返回 `2`。第一次失败后，函数立即返回，不再进行第二次除法。

### 使用条件

1. `?` 只能作为函数体中 `let` 的直接初始化式，例如 `let value = result?;`，不能嵌入算术表达式或函数实参。
2. 对 `Option<T>` 使用 `?` 时，函数必须返回 `Option`；对 `Result<T, E>` 使用时，函数必须返回 `Result`，且能够接收传播的错误类型。成功载荷可以与函数的最终返回载荷类型不同。
3. `?` 不能用于 contract、predicate、flow 或 workflow。

使用 `build` 或 `run` 时，成功载荷目前须为 `Bool` 或 `Int`（包括有界整数）；其他成功载荷可以通过 `check`。

完整规则见[语言规范的 Try 运算符小节](../spec/core-language.zh.md#469-try-运算符)。
