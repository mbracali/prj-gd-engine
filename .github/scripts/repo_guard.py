#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Repo guard: keeps game data, reverse-engineering output, large binaries,
signing keys and personal paths out of the repo. Checks every tracked file
(git ls-files), so it runs the same in CI and locally:

    python3 .github/scripts/repo_guard.py
"""
import os
import re
import subprocess
import sys

MAX_BYTES = 20 * 1024 * 1024

# Game data, RE output, build output, keys, and the downloaded DLSS SDK.
FORBIDDEN = re.compile(
    r"\.(iso|bin|cue|img|chd|xex|ark|hdr|vgs|bik|mogg|apk|aab|keystore|jks|gpr)$"
    r"|\.milo"
    r"|(^|/)(original_rom|original_files|original_isos|original_iso|extracted[^/]*|analysis|ghidra|decomp|rips|local_builds)/"
    r"|(^|/)third_party/dlss/(?!README\.md$)",
    re.IGNORECASE,
)
# Placeholders that keep the git-ignored folders in the tree.
PLACEHOLDER = re.compile(r"(^|/)(original_isos|extracted|ghidra|local_builds|logs)/(README\.md|\.gitkeep)$")
PERSONAL_PATH = re.compile(r"/home/[a-z][a-z0-9_-]*/|/Users/[A-Za-z][^/\s]*/|[A-Za-z]:\\Users\\")
# Decompiler output pasted as code (Ghidra names or types outside comments).
DECOMP = re.compile(r"\bundefined[1248]\b|\b(FUN|DAT|LAB|PTR)_[0-9a-fA-F]{6,8}\b")
CODE_EXT = (".c", ".cpp", ".h", ".hpp", ".java")
SELF = ".github/scripts/repo_guard.py"


def code_part(line):
    s = line.strip()
    if s.startswith(("//", "*", "/*")):
        return ""
    return line.split("//")[0]


def main():
    files = subprocess.run(["git", "ls-files", "-z"], capture_output=True, check=True).stdout.decode().split("\0")
    problems = []
    for f in filter(None, files):
        if not os.path.isfile(f):
            continue
        if FORBIDDEN.search(f) and not PLACEHOLDER.search(f):
            problems.append(f"{f}: game data, RE output, build output, signing key or DLSS SDK file")
        size = os.path.getsize(f)
        if size > MAX_BYTES:
            problems.append(f"{f}: {size / 1048576:.1f} MB (limit {MAX_BYTES // 1048576} MB)")
        if f == SELF or size > 4 * 1024 * 1024:
            continue
        try:
            text = open(f, encoding="utf-8").read()
        except (UnicodeDecodeError, OSError):
            continue  # binary
        for n, line in enumerate(text.splitlines(), 1):
            if PERSONAL_PATH.search(line):
                problems.append(f"{f}:{n}: personal path: {line.strip()[:100]}")
            if f.endswith(CODE_EXT) and "/third_party/" not in f and DECOMP.search(code_part(line)):
                problems.append(f"{f}:{n}: decompiler name or type in code: {line.strip()[:100]}")
    for p in problems:
        print(f"::error::{p}" if os.environ.get("GITHUB_ACTIONS") else p)
    print(f"repo guard: {len(files) - 1} files checked, {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
