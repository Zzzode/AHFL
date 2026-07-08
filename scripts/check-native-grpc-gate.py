#!/usr/bin/env python3
"""Fail closed on native gRPC implementation before RFC0004 is accepted."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RFC0004 = ROOT / "docs" / "rfcs" / "0004-native-grpc-transport.zh.md"
DECISION_GATE = ROOT / "docs" / "plans" / "native-grpc-decision-gate.zh.md"
SELF = Path(__file__).resolve()

NATIVE_ALLOWED_STATUSES = {"accepted", "implementing", "implemented", "stabilized"}
SKIP_DIRS = {
    ".git",
    ".cache",
    ".idea",
    ".vscode",
    "build",
    "cmake-build-debug",
    "cmake-build-release",
    "node_modules",
    "third_party",
}
SCAN_SUFFIXES = {
    ".cmake",
    ".cpp",
    ".cxx",
    ".cc",
    ".hpp",
    ".hxx",
    ".h",
    ".proto",
    ".py",
    ".sh",
    ".yml",
    ".yaml",
    ".txt",
}
EXEMPT_PATHS = {
    RFC0004,
    DECISION_GATE,
    SELF,
}

FORBIDDEN_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    (
        "native gRPC build flag",
        re.compile(r"\bAHFL_ENABLE_GRPC_NATIVE\b"),
    ),
    (
        "native gRPC C++ include",
        re.compile(r"^\s*#\s*include\s*[<\"]grpcpp/", re.MULTILINE),
    ),
    (
        "native Protobuf C++ include",
        re.compile(r"^\s*#\s*include\s*[<\"]google/protobuf/", re.MULTILINE),
    ),
    (
        "CMake gRPC package lookup",
        re.compile(r"\bfind_package\s*\(\s*gRPC\b", re.IGNORECASE),
    ),
    (
        "CMake Protobuf package lookup",
        re.compile(r"\bfind_package\s*\(\s*Protobuf\b", re.IGNORECASE),
    ),
    (
        "CMake gRPC FetchContent wiring",
        re.compile(r"\bFetchContent_(?:Declare|MakeAvailable)\s*\([^)]*\bgrpc\b", re.IGNORECASE),
    ),
    (
        "CMake gRPC target link",
        re.compile(r"\bgRPC::grpc\+\+\b|\bgrpc::grpc\+\+\b"),
    ),
    (
        "CMake Protobuf target link",
        re.compile(r"\bprotobuf::libprotobuf\b|\bprotobuf::protoc\b"),
    ),
    (
        "native gRPC proto service contract",
        re.compile(r"\bservice\s+(?:LLMInference|StreamingInference|ToolCalling)\b"),
    ),
)


def frontmatter_status(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    if not text.startswith("---\n"):
        raise RuntimeError(f"{path.relative_to(ROOT)}: missing YAML frontmatter")
    end = text.find("\n---", 4)
    if end == -1:
        raise RuntimeError(f"{path.relative_to(ROOT)}: unterminated YAML frontmatter")
    for line in text[4:end].splitlines():
        if line.startswith("status:"):
            return line.split(":", 1)[1].strip().strip("\"'")
    raise RuntimeError(f"{path.relative_to(ROOT)}: missing status field")


def should_scan(path: Path) -> bool:
    if path in EXEMPT_PATHS:
        return False
    rel = path.relative_to(ROOT)
    if any(part in SKIP_DIRS for part in rel.parts):
        return False
    if path.name in {"package-lock.json", "compile_commands.json"}:
        return False
    return path.suffix in SCAN_SUFFIXES or path.name in {"CMakeLists.txt"}


def main() -> int:
    status = frontmatter_status(RFC0004)
    if status in NATIVE_ALLOWED_STATUSES:
        print(f"RFC0004 status is {status}; native gRPC gate allows implementation markers.")
        return 0

    failures: list[str] = []
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file() or not should_scan(path):
            continue
        if re.search(r"\bnative[_-]grpc\b", path.name, re.IGNORECASE):
            failures.append(
                f"{path.relative_to(ROOT)}: native gRPC source file is forbidden while RFC0004 "
                f"status is {status!r}. Complete the native gRPC decision gate first."
            )
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for label, pattern in FORBIDDEN_PATTERNS:
            match = pattern.search(text)
            if match is None:
                continue
            line = text.count("\n", 0, match.start()) + 1
            failures.append(
                f"{path.relative_to(ROOT)}:{line}: {label} is forbidden while RFC0004 "
                f"status is {status!r}. Complete the native gRPC decision gate first."
            )

    if failures:
        print("Native gRPC gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(f"Native gRPC gate passed: RFC0004 status is {status}; no native markers found.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
