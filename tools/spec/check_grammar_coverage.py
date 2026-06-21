#!/usr/bin/env python3
"""AHFL grammar <-> spec EBNF 一致性门禁。

本脚本对比两个非终结符集合，发现 spec 与 grammar 之间的漂移缺口：

1. **grammar rules**: 从 `grammar/AHFL.g4` 解析所有 parser/lexer 规则名
   （`ruleName:` 与 `TOKEN:` 形式）。
2. **spec non-terminals**: 从一份或多份 spec Markdown 解析所有 EBNF 非终结符
   （`Name ::=` 形式，跨越代码块）。

输出三类差异：

* `only_in_grammar`: grammar 实现但 spec 未提及 —— 通常是 spec 漂移缺口（P0）。
* `only_in_spec`: spec 提及但 grammar 没有 —— 可能是 spec 编造或命名漂移。
* `matched`: 双方都有的规则名。

退出码：

* `0`: 无差异（或只允许 known 例外）。
* `1`: 存在非空 `only_in_grammar` 或 `only_in_spec`。

使用示例::

    # 默认对比 grammar/AHFL.g4 与 docs/spec/core-language.zh.md
    python3 tools/spec/check_grammar_coverage.py

    # 指定多份 spec
    python3 tools/spec/check_grammar_coverage.py \
        --grammar grammar/AHFL.g4 \
        --spec docs/spec/core-language.zh.md docs/spec/assurance.zh.md

    # 关闭 known-aliases 白名单（输出裸 diff）
    python3 tools/spec/check_grammar_coverage.py --strict

设计说明：

* 仅做名称级对比，不做 AST/precedence 深度比较 —— 目的是把"spec 漏写 grammar 规则"
  这类结构性漂移挡在 CI，而不是替代形式化 grammar 等价性证明。
* spec EBNF 使用类 W3C 风格（`Name ::= ...`），与 `docs/spec/core-language.zh.md`
  现有写法一致；grammar 解析容忍 ANTLR4 注释与 `-> skip` 动作。
* 故意不去除 grammar 中的 `type_` 这类尾部下划线 —— spec 侧通过 `--alias`
  显式声明等价映射（如 `type_ == Type`），避免静默吞掉命名差异。
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


# ---------------------------------------------------------------------------
# 解析
# ---------------------------------------------------------------------------

# ANTLR4 规则名：行首或缩进后，合法标识符紧跟 ':'（非 '::='，那是 spec 的 EBNF）。
# lexer token 全大写、parser rule 小写开头，二者都收。
_GRAMMAR_RULE_RE = re.compile(
    r"""(?xm)
    ^ [ \t]*
    (?P<name> [A-Za-z_][A-Za-z0-9_]* )
    [ \t]* : [ \t]* [^:]
    """
)

# spec EBNF 非终结符：`Name ::=`，Name 为大驼峰或小驼峰标识符。
_SPEC_NONTERM_RE = re.compile(
    r"""(?xm)
    ^ [ \t]*
    (?P<name> [A-Za-z_][A-Za-z0-9_]* )
    [ \t]* ::=
    """
)


@dataclass
class CoverageReport:
    grammar_rules: set[str] = field(default_factory=set)
    spec_nonterms: set[str] = field(default_factory=set)
    matched: set[str] = field(default_factory=set)
    only_in_grammar: set[str] = field(default_factory=set)
    only_in_spec: set[str] = field(default_factory=set)
    # lexer tokens 仅作为信息性附录展示，不强制 spec 暴露全集。
    lexer_tokens: set[str] = field(default_factory=set)

    def has_drift(self) -> bool:
        return bool(self.only_in_grammar or self.only_in_spec)

    def has_hard_drift(self) -> bool:
        """硬漂移：grammar 实现但 spec 未文档化。

        这是 P0 漂移缺口的判定依据：spec 是规范性来源，任何 grammar 中
        存在但 spec 未提及的 parser rule，都意味着读者无法仅靠 spec 理解语言。
        """
        return bool(self.only_in_grammar)


def parse_spec(path: Path) -> set[str]:
    """从 Markdown spec 提取 EBNF 非终结符名。

    跨越 ```ebnf / ```text 围栏代码块均识别，只要行内出现 `Name ::=`。
    """
    text = path.read_text(encoding="utf-8")
    names: set[str] = set()
    for line in text.splitlines():
        m = _SPEC_NONTERM_RE.match(line)
        if m:
            names.add(m.group("name"))
    return names


def parse_grammar(path: Path) -> tuple[set[str], set[str]]:
    """从 ANTLR4 grammar 文件提取 (parser_rules, lexer_tokens) 两组名字。

    * parser_rules: 首字母小写的规则，对应 spec 的 EBNF 非终结符。
    * lexer_tokens: 全大写的 token，对应 spec 的字面量/词法规则（不强制 1:1）。
    """
    text = path.read_text(encoding="utf-8")
    parser_rules: set[str] = set()
    lexer_tokens: set[str] = set()
    for line in text.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("//") or stripped.startswith("/*"):
            continue
        m = _GRAMMAR_RULE_RE.match(line)
        if not m:
            continue
        name = m.group("name")
        if name == "fragment":
            continue
        # 全大写（允许下划线/数字）视为 lexer token。
        if name.isupper():
            lexer_tokens.add(name)
        else:
            parser_rules.add(name)
    return parser_rules, lexer_tokens


# ---------------------------------------------------------------------------
# known alias 映射
# ---------------------------------------------------------------------------

# grammar 与 spec 之间已知的、**有意**的命名差异（grammar 名 → spec 名）。
# 仅收录 grammar parser rule 名与 spec 大驼峰名不一致、且无法靠首字母大写规则推断的情况。
DEFAULT_ALIASES: dict[str, str] = {
    # ANTLR4 不允许规则名与内置关键字冲突，故 grammar 用 `type_`，spec 用 `Type`。
    "type_": "Type",
    # grammar 的 `integerLiteral` 在 spec 中写作 `IntLiteral`（更短），同一概念。
    "integerLiteral": "IntLiteral",
}


def normalize_parser_rules(rules: set[str], aliases: dict[str, str]) -> set[str]:
    """把 grammar parser rule 名归一化到 spec 命名空间。

    策略：

    1. 显式 alias 优先（如 `type_` → `Type`）。
    2. 其余小驼峰名首字母大写（`addExpr` → `AddExpr`），与 spec EBNF 习惯对齐。
    """
    out: set[str] = set()
    for name in rules:
        if name in aliases:
            out.add(aliases[name])
        else:
            out.add(name[:1].upper() + name[1:])
    return out


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

def build_report(
    grammar_path: Path,
    spec_paths: list[Path],
    aliases: dict[str, str] | None,
) -> CoverageReport:
    parser_rules, lexer_tokens = parse_grammar(grammar_path)
    spec_raw: set[str] = set()
    for p in spec_paths:
        spec_raw |= parse_spec(p)

    effective_aliases = aliases if aliases is not None else {}
    grammar_set = normalize_parser_rules(parser_rules, effective_aliases)
    spec_set = set(spec_raw)

    return CoverageReport(
        grammar_rules=grammar_set,
        spec_nonterms=spec_set,
        matched=grammar_set & spec_set,
        only_in_grammar=grammar_set - spec_set,
        only_in_spec=spec_set - grammar_set,
        lexer_tokens=lexer_tokens,
    )


def format_report(report: CoverageReport, grammar_path: Path, spec_paths: list[Path]) -> str:
    lines: list[str] = []
    lines.append("AHFL grammar <-> spec EBNF 一致性报告")
    lines.append(
        f"  grammar: {grammar_path}  ({len(report.grammar_rules)} parser rules, "
        f"{len(report.lexer_tokens)} lexer tokens)"
    )
    lines.append(f"  spec:    {', '.join(str(p) for p in spec_paths)}  ({len(report.spec_nonterms)} nonterms)")
    lines.append(f"  matched: {len(report.matched)}")
    lines.append("")

    def render(title: str, items: set[str]) -> None:
        lines.append(f"{title} ({len(items)}):")
        if not items:
            lines.append("  (none)")
        else:
            for name in sorted(items):
                lines.append(f"  - {name}")
        lines.append("")

    render("[DRIFT] only_in_grammar (grammar 实现但 spec 未提及)", report.only_in_grammar)
    render("[DRIFT] only_in_spec (spec 提及但 grammar 没有)", report.only_in_spec)
    render("[INFO] lexer tokens (词法规则，spec 不强制暴露全集)", report.lexer_tokens)
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="AHFL grammar <-> spec EBNF 一致性门禁",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    repo_root = Path(__file__).resolve().parents[2]
    parser.add_argument(
        "--grammar",
        type=Path,
        default=repo_root / "grammar" / "AHFL.g4",
        help="ANTLR4 grammar 文件路径（默认 grammar/AHFL.g4）",
    )
    parser.add_argument(
        "--spec",
        nargs="+",
        type=Path,
        default=[repo_root / "docs" / "spec" / "core-language.zh.md"],
        help="参与对比的 spec Markdown 文件（可多个）",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="关闭默认 alias 映射（如 type_→Type），仅做首字母大写归一化，"
             "用于发现新的命名漂移",
    )
    parser.add_argument(
        "--quiet", "-q",
        action="store_true",
        help="只在有漂移时输出报告",
    )
    parser.add_argument(
        "--fail-on-spec-only",
        action="store_true",
        help="对 only_in_spec 也视为失败。默认 only_in_spec 仅警告，因为 spec "
             "常用聚合抽象非终结符（如 Statement/Type/PrimaryExpr），这些在 "
             "grammar 中被内联展开，并非真漂移。only_in_grammar 始终视为失败。",
    )
    args = parser.parse_args(argv)

    aliases: dict[str, str] | None = {} if args.strict else DEFAULT_ALIASES

    missing = [p for p in [args.grammar, *args.spec] if not p.exists()]
    if missing:
        print("ERROR: 文件不存在:", ", ".join(str(p) for p in missing), file=sys.stderr)
        return 2

    report = build_report(args.grammar, args.spec, aliases)

    if report.has_drift() or not args.quiet:
        print(format_report(report, args.grammar, args.spec))

    failed = report.has_hard_drift() or (args.fail_on_spec_only and bool(report.only_in_spec))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
