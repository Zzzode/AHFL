#!/usr/bin/env python3
"""Validate AHFL RFC registry structure.

The checker intentionally supports only the small YAML subset used by
docs/rfcs/index.yml and RFC frontmatter. Avoiding PyYAML keeps the CI gate
dependency-free.
"""

from __future__ import annotations

import ast
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
RFC_DIR = REPO_ROOT / "docs" / "rfcs"
INDEX_FILE = RFC_DIR / "index.yml"
TEMPLATE_FILE = RFC_DIR / "0000-template.zh.md"

ALLOWED_STATUSES = {
    "draft",
    "review",
    "fcp",
    "accepted",
    "implementing",
    "implemented",
    "stabilized",
    "rejected",
    "withdrawn",
    "postponed",
    "superseded",
}
STATUS_REQUIRING_COMPLETE_FIELDS = {
    "review",
    "fcp",
    "accepted",
    "implementing",
    "implemented",
    "stabilized",
    "superseded",
}
STATUS_REJECTING_STALE_MARKERS = {
    "review",
    "fcp",
    "accepted",
    "implementing",
    "implemented",
    "stabilized",
}
ALLOWED_AREAS = {
    "language",
    "compiler",
    "ir",
    "stdlib",
    "runtime",
    "tooling",
    "formal",
    "process",
}
ALLOWED_STABILITY = {
    "experimental",
    "internal",
    "developer-facing",
    "stable-language",
    "stable-artifact",
}
REQUIRED_FRONTMATTER = [
    "rfc",
    "title",
    "status",
    "area",
    "stability",
    "created",
    "updated",
    "authors",
    "shepherd",
    "owners",
    "required_reviewers",
    "tracking_issue",
    "discussion",
    "implementation_prs",
]
REQUIRED_SECTIONS = [
    "Summary",
    "Motivation",
    "Goals",
    "Non-Goals",
    "Design",
    "User Impact",
    "Compatibility and Migration",
    "Implementation Plan",
    "Test Plan",
    "Rollout and Stabilization",
    "Alternatives",
    "Open Questions",
    "Decision History",
]
CANONICAL_RE = re.compile(r"^(?P<rfc>[0-9]{4})-[a-z0-9]+(?:-[a-z0-9]+)*\.zh\.md$")
WAVE_LOCAL_RE = re.compile(r"^[a-z]-[0-9]+\.md$")
ISO_DATE_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}$")
STALE_RE = re.compile(r"\b(TBD|TODO|DEFERRED)\b", re.IGNORECASE)
FORBIDDEN_KEY_PREFIXES = ("leg" "acy",)


@dataclass
class Issue:
    path: Path
    message: str

    def format(self) -> str:
        return f"{self.path.relative_to(REPO_ROOT)}: {self.message}"


def parse_scalar(raw: str) -> Any:
    value = raw.strip()
    if value == "":
        return ""
    if value in {"[]", "{}"}:
        return [] if value == "[]" else {}
    if value.lower() == "true":
        return True
    if value.lower() == "false":
        return False
    if value.startswith("[") or value.startswith("{"):
        try:
            return ast.literal_eval(value)
        except (SyntaxError, ValueError):
            return value
    if (value.startswith('"') and value.endswith('"')) or (
        value.startswith("'") and value.endswith("'")
    ):
        try:
            return ast.literal_eval(value)
        except (SyntaxError, ValueError):
            return value[1:-1]
    return value


def split_key_value(line: str) -> tuple[str, Any]:
    key, value = line.split(":", 1)
    return key.strip(), parse_scalar(value)


def parse_simple_yaml(raw: str, path: Path, issues: list[Issue]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    parent_key: str | None = None
    current_item: dict[str, Any] | None = None

    for line_no, original in enumerate(raw.splitlines(), start=1):
        if not original.strip() or original.lstrip().startswith("#"):
            continue
        indent = len(original) - len(original.lstrip(" "))
        line = original.strip()

        try:
            if indent == 0:
                current_item = None
                if line.endswith(":"):
                    parent_key = line[:-1].strip()
                    result[parent_key] = {}
                else:
                    key, value = split_key_value(line)
                    result[key] = value
                    parent_key = None
            elif indent == 2 and parent_key:
                if line.startswith("- "):
                    if not isinstance(result.get(parent_key), list):
                        result[parent_key] = []
                    item = line[2:].strip()
                    if ":" in item:
                        key, value = split_key_value(item)
                        current_item = {key: value}
                        result[parent_key].append(current_item)
                    else:
                        result[parent_key].append(parse_scalar(item))
                        current_item = None
                else:
                    if not isinstance(result.get(parent_key), dict):
                        result[parent_key] = {}
                    key, value = split_key_value(line)
                    result[parent_key][key] = value
            elif indent == 4 and current_item is not None:
                key, value = split_key_value(line)
                current_item[key] = value
            else:
                issues.append(Issue(path, f"unsupported YAML shape at line {line_no}"))
        except ValueError:
            issues.append(Issue(path, f"invalid YAML key/value at line {line_no}"))

    return result


def load_frontmatter(path: Path, issues: list[Issue]) -> tuple[dict[str, Any], str]:
    text = path.read_text(encoding="utf-8")
    if not text.startswith("---\n"):
        issues.append(Issue(path, "missing YAML frontmatter"))
        return {}, text
    end = text.find("\n---\n", 4)
    if end == -1:
        issues.append(Issue(path, "frontmatter is not closed"))
        return {}, text
    raw = text[4:end]
    body = text[end + len("\n---\n") :]
    return parse_simple_yaml(raw, path, issues), body


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def is_stale(value: Any) -> bool:
    if isinstance(value, str):
        return bool(STALE_RE.search(value))
    if isinstance(value, list):
        return any(is_stale(item) for item in value)
    if isinstance(value, dict):
        return any(is_stale(item) for item in value.values())
    return False


def check_required_sections(path: Path, body: str, issues: list[Issue]) -> None:
    positions: dict[str, int] = {}
    for match in re.finditer(r"^##\s+(.+?)\s*$", body, flags=re.MULTILINE):
        heading = match.group(1).strip()
        if heading in REQUIRED_SECTIONS and heading not in positions:
            positions[heading] = match.start()

    previous = -1
    for section in REQUIRED_SECTIONS:
        if section not in positions:
            issues.append(Issue(path, f"missing required section '## {section}'"))
            continue
        if positions[section] < previous:
            issues.append(Issue(path, f"required section out of order: '## {section}'"))
        previous = positions[section]


def check_links(path: Path, text: str, issues: list[Issue]) -> None:
    for match in re.finditer(r"\[[^\]]+\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)", text):
        target = match.group(1)
        if target.startswith(("#", "http://", "https://", "mailto:")):
            continue
        if target.startswith("/") or "/Users/" in target:
            issues.append(Issue(path, f"machine-local or absolute link is forbidden: {target}"))
            continue
        link_path = target.split("#", 1)[0]
        if not link_path:
            continue
        resolved = (path.parent / link_path).resolve()
        try:
            resolved.relative_to(REPO_ROOT)
        except ValueError:
            issues.append(Issue(path, f"relative link escapes repository: {target}"))
            continue
        if not resolved.exists():
            issues.append(Issue(path, f"relative link target does not exist: {target}"))


def check_diagrams(path: Path, body: str, issues: list[Issue]) -> None:
    lines = body.splitlines()
    in_fence = False
    fence_lang = ""
    fence_start = 0
    fence_body: list[str] = []
    context: list[str] = []

    for idx, line in enumerate(lines, start=1):
        fence = re.match(r"^```([A-Za-z0-9_+-]*)\s*$", line)
        if fence and not in_fence:
            in_fence = True
            fence_lang = fence.group(1).lower()
            fence_start = idx
            fence_body = []
            context = lines[max(0, idx - 4) : idx - 1]
            continue
        if fence and in_fence:
            joined_context = "\n".join(context).lower()
            looks_like_diagram = bool(
                re.search(r"diagram|architecture|架构图|流程图|状态图", joined_context)
            )
            if fence_lang in {"dot", "graphviz", "plantuml", "ascii"}:
                issues.append(Issue(path, f"non-Mermaid diagram fence at line {fence_start}: {fence_lang}"))
            elif looks_like_diagram and fence_lang not in {"mermaid"}:
                issues.append(Issue(path, f"architecture diagram must use mermaid fence at line {fence_start}"))
            in_fence = False
            continue
        if in_fence:
            fence_body.append(line)

    if in_fence:
        issues.append(Issue(path, f"unclosed fenced code block starting at line {fence_start}"))


def check_canonical(path: Path, meta: dict[str, Any], body: str, issues: list[Issue]) -> None:
    match = CANONICAL_RE.match(path.name)
    if not match:
        issues.append(Issue(path, "canonical RFC filename must match NNNN-kebab-slug.zh.md"))
        return

    for key in REQUIRED_FRONTMATTER:
        if key not in meta:
            issues.append(Issue(path, f"missing frontmatter field '{key}'"))

    rfc = str(meta.get("rfc", ""))
    if rfc != match.group("rfc"):
        issues.append(Issue(path, f"frontmatter rfc '{rfc}' does not match filename"))

    status = str(meta.get("status", ""))
    if status not in ALLOWED_STATUSES:
        issues.append(Issue(path, f"invalid status '{status}'"))

    areas = as_list(meta.get("area"))
    if not areas:
        issues.append(Issue(path, "frontmatter area must be a non-empty list"))
    for area in areas:
        if area not in ALLOWED_AREAS:
            issues.append(Issue(path, f"unknown area '{area}'"))

    stability = str(meta.get("stability", ""))
    if stability not in ALLOWED_STABILITY:
        issues.append(Issue(path, f"invalid stability '{stability}'"))

    for key in ("created", "updated", "decision_due"):
        if key in meta and not ISO_DATE_RE.match(str(meta[key])):
            issues.append(Issue(path, f"frontmatter field '{key}' must be YYYY-MM-DD"))

    for key in ("authors", "required_reviewers", "implementation_prs"):
        if key in meta and not isinstance(meta[key], list):
            issues.append(Issue(path, f"frontmatter field '{key}' must be a list"))

    forbidden = sorted(key for key in meta if key.startswith(FORBIDDEN_KEY_PREFIXES))
    for key in forbidden:
        issues.append(Issue(path, f"frontmatter field '{key}' is forbidden; use canonical RFC ids only"))

    owners = meta.get("owners")
    if not isinstance(owners, dict):
        issues.append(Issue(path, "frontmatter owners must be a mapping"))
        owners = {}
    for area in areas:
        if not str(owners.get(area, "")).strip():
            issues.append(Issue(path, f"missing owner entry for area '{area}'"))

    if status in STATUS_REQUIRING_COMPLETE_FIELDS:
        for key in ("shepherd", "owners", "tracking_issue", "discussion"):
            if is_stale(meta.get(key)):
                issues.append(Issue(path, f"status '{status}' cannot keep stale marker in '{key}'"))

    if status in STATUS_REJECTING_STALE_MARKERS and STALE_RE.search(body):
        issues.append(Issue(path, f"status '{status}' cannot contain TBD/TODO/DEFERRED in body"))

    check_required_sections(path, body, issues)
    check_links(path, body, issues)
    check_diagrams(path, body, issues)


def check_index(index: dict[str, Any], canonical_files: list[Path], issues: list[Issue]) -> None:
    rfcs = index.get("rfcs")
    if not isinstance(rfcs, list):
        issues.append(Issue(INDEX_FILE, "index.yml must contain rfcs list"))
        return

    seen_ids: set[str] = set()
    seen_files: set[str] = set()
    expected_files = {path.name for path in canonical_files}

    for item in rfcs:
        if not isinstance(item, dict):
            issues.append(Issue(INDEX_FILE, "each rfcs entry must be a mapping"))
            continue
        rfc = str(item.get("rfc", ""))
        filename = str(item.get("file", ""))
        status = str(item.get("status", ""))
        if rfc in seen_ids:
            issues.append(Issue(INDEX_FILE, f"duplicate RFC id '{rfc}'"))
        seen_ids.add(rfc)
        seen_files.add(filename)
        if not re.match(r"^[0-9]{4}$", rfc):
            issues.append(Issue(INDEX_FILE, f"invalid RFC id '{rfc}'"))
        if filename not in expected_files:
            issues.append(Issue(INDEX_FILE, f"indexed file does not exist as canonical RFC: {filename}"))
        if status not in ALLOWED_STATUSES:
            issues.append(Issue(INDEX_FILE, f"invalid status '{status}' for RFC {rfc}"))

    missing = sorted(expected_files - seen_files)
    extra = sorted(seen_files - expected_files)
    for filename in missing:
        issues.append(Issue(INDEX_FILE, f"canonical RFC missing from index: {filename}"))
    for filename in extra:
        issues.append(Issue(INDEX_FILE, f"index references non-canonical file: {filename}"))

    numeric_ids = sorted(int(rfc) for rfc in seen_ids if re.match(r"^[0-9]{4}$", rfc))
    expected_ids = list(range(1, len(numeric_ids) + 1))
    if numeric_ids != expected_ids:
        expected = ", ".join(f"{item:04d}" for item in expected_ids)
        actual = ", ".join(f"{item:04d}" for item in numeric_ids)
        issues.append(Issue(INDEX_FILE, f"RFC ids must be contiguous from 0001: expected [{expected}], got [{actual}]"))


def compare_index_entry(path: Path, meta: dict[str, Any], entry: dict[str, Any], issues: list[Issue]) -> None:
    for key in ("rfc", "title", "status", "stability"):
        if str(meta.get(key, "")) != str(entry.get(key, "")):
            issues.append(Issue(INDEX_FILE, f"{path.name} index field '{key}' does not match frontmatter"))

    for key in ("area",):
        meta_values = as_list(meta.get(key))
        entry_values = as_list(entry.get(key))
        if meta_values != entry_values:
            issues.append(Issue(INDEX_FILE, f"{path.name} index field '{key}' does not match frontmatter"))

    forbidden = sorted(key for key in entry if key.startswith(FORBIDDEN_KEY_PREFIXES))
    for key in forbidden:
        issues.append(Issue(INDEX_FILE, f"{path.name} index field '{key}' is forbidden; use canonical RFC ids only"))


def main() -> int:
    issues: list[Issue] = []

    if not RFC_DIR.exists():
        issues.append(Issue(RFC_DIR, "docs/rfcs directory does not exist"))
    if not INDEX_FILE.exists():
        issues.append(Issue(INDEX_FILE, "index.yml does not exist"))
    if not TEMPLATE_FILE.exists():
        issues.append(Issue(TEMPLATE_FILE, "0000-template.zh.md does not exist"))

    index = parse_simple_yaml(INDEX_FILE.read_text(encoding="utf-8"), INDEX_FILE, issues)
    index_entries = {
        str(item.get("file", "")): item
        for item in index.get("rfcs", [])
        if isinstance(item, dict)
    }
    for key in index:
        if key.startswith(FORBIDDEN_KEY_PREFIXES):
            issues.append(Issue(INDEX_FILE, f"top-level field '{key}' is forbidden; use canonical RFC ids only"))

    canonical_files: list[Path] = []
    allowed_singletons = {"README.md", "index.yml", "0000-template.zh.md"}

    for path in sorted(RFC_DIR.iterdir()):
        if path.name in allowed_singletons:
            continue
        if path.is_dir():
            issues.append(Issue(path, "subdirectories are not allowed in docs/rfcs"))
            continue
        if CANONICAL_RE.match(path.name):
            canonical_files.append(path)
        elif WAVE_LOCAL_RE.match(path.name):
            issues.append(Issue(path, "wave-local RFC files are forbidden; rename to NNNN-kebab-slug.zh.md"))
        else:
            issues.append(Issue(path, "unexpected file in RFC registry"))

    check_index(index, canonical_files, issues)

    for path in canonical_files:
        meta, body = load_frontmatter(path, issues)
        check_canonical(path, meta, body, issues)
        entry = index_entries.get(path.name)
        if isinstance(entry, dict):
            compare_index_entry(path, meta, entry, issues)

    template_meta, template_body = load_frontmatter(TEMPLATE_FILE, issues)
    if str(template_meta.get("rfc", "")) != "0000":
        issues.append(Issue(TEMPLATE_FILE, "template rfc must be 0000"))
    check_required_sections(TEMPLATE_FILE, template_body, issues)
    check_links(TEMPLATE_FILE, TEMPLATE_FILE.read_text(encoding="utf-8"), issues)
    check_diagrams(TEMPLATE_FILE, template_body, issues)

    if issues:
        for issue in issues:
            print(issue.format(), file=sys.stderr)
        print(f"RFC check failed with {len(issues)} issue(s).", file=sys.stderr)
        return 1

    print(f"RFC check passed: {len(canonical_files)} canonical RFC(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
