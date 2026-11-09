#!/usr/bin/env python3
"""Leakage gate for the public API headers (M9.8, extended in M12.5).

Scans public headers (recursively) for backend symbols (Vulkan, VMA, SDL,
GLM) and internal include paths. Public headers must be self-contained and
backend-neutral per ADR-0009. Comments are stripped before scanning so
documentation mentions do not false-positive.

Usage:
    check_public_leakage.py <include-dir>
    check_public_leakage.py --self-test   # verifies the gate itself fails
                                          # on a deliberately poisoned header
"""

import re
import sys
import tempfile
from pathlib import Path

FORBIDDEN = [
    (re.compile(r'#\s*include\s*[<"][^>"]*(vulkan|SDL|glm|vma|miniaudio)', re.IGNORECASE), "backend include"),
    (re.compile(r'\b(Vk[A-Z][A-Za-z0-9]+|VK_[A-Z0-9_]+)\b'), "Vulkan symbol"),
    (re.compile(r'\bVMA_[A-Z0-9_]+\b|\bVma[A-Z]'), "VMA reference"),
    (re.compile(r'\bSDL_[A-Za-z0-9_]+\b'), "SDL symbol"),
    (re.compile(r'\bglm::'), "GLM type"),
    (re.compile(r'\bma_[a-zA-Z0-9_]+'), "miniaudio symbol"),
    (re.compile(r'#\s*include\s*"[^"]*(platform/|environment/|spatial/|gpu_resources/|vulkan/|core/|math/math|audio/)'),
     "internal include"),
    (re.compile(r'\bsrc/'), "internal path reference"),
]

_COMMENT = re.compile(r'//.*?$|/\*.*?\*/', re.DOTALL | re.MULTILINE)


def strip_comments(text: str) -> str:
    """Remove comments while preserving line numbers."""

    def blank(match: re.Match) -> str:
        return "".join("\n" if char == "\n" else " " for char in match.group(0))

    return _COMMENT.sub(blank, text)


def scan_directory(directory: Path) -> list[str]:
    findings: list[str] = []
    headers = sorted(directory.rglob("*.hpp"))
    if not headers:
        findings.append(f"no public headers found in {directory}")
        return findings
    for header in headers:
        clean_text = strip_comments(header.read_text(encoding="utf-8"))
        for pattern, label in FORBIDDEN:
            for match in pattern.finditer(clean_text):
                line = clean_text.count("\n", 0, match.start()) + 1
                findings.append(f"{header.name}:{line}: {label}: {match.group(0)!r}")
    return findings


def self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        poisoned = root / "poisoned.hpp"
        poisoned.write_text(
            "#pragma once\n#include <vulkan/vulkan.h>\nVkDevice handle;\nuint32_t val = VK_NULL_HANDLE;\n",
            encoding="utf-8",
        )
        findings = scan_directory(root)
        if len(findings) < 3:
            print(f"self-test FAILED: expected >=3 findings, got {findings}")
            return 1

        good = root / "good.hpp"
        poisoned.unlink()
        good.write_text(
            "#pragma once\n// Comments mentioning VkDevice are allowed\n#include <cstdint>\n",
            encoding="utf-8",
        )
        findings = scan_directory(root)
        if findings:
            print(f"self-test FAILED: clean header flagged: {findings}")
            return 1

    print("self-test passed: gate rejects poisoned headers and accepts clean ones")
    return 0


def main() -> int:
    args = sys.argv[1:]
    if args == ["--self-test"]:
        return self_test()
    if len(args) != 1:
        print(__doc__)
        return 2
    findings = scan_directory(Path(args[0]))
    if findings:
        print("public header leakage detected:")
        for finding in findings:
            print(f"  {finding}")
        return 1
    print("public header leakage scan passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
