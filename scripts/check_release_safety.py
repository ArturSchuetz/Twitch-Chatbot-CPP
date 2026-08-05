#!/usr/bin/env python3
"""Reject credentials and generated/private artifacts before publication."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


LOCAL_EXCLUDED_DIRECTORIES = {
    ".git",
    ".vs",
    ".cache",
    "__pycache__",
    "vcpkg_installed",
}
FORBIDDEN_TRACKED_DIRECTORIES = {"bin", "obj", "out", "vcpkg_installed"}
GENERATED_SUFFIXES = {
    ".dll",
    ".exe",
    ".idb",
    ".ilk",
    ".iobj",
    ".ipdb",
    ".log",
    ".obj",
    ".pdb",
    ".pyc",
    ".tlog",
}
PLACEHOLDER_PATTERN = re.compile(
    r"^(?:replace(?:_me)?|change(?:_me)?|your(?:_[a-z0-9_]+)?|example(?:_[a-z0-9_]+)?|<[^>]+>|\$\{.+\})$",
    re.IGNORECASE,
)
SECRET_PATTERNS = (
    (
        "OAuth-prefixed value",
        re.compile(r"oauth:[A-Za-z0-9_-]{10,}", re.IGNORECASE),
        False,
    ),
    (
        "TWITCH_OAUTH_TOKEN assignment",
        re.compile(r"TWITCH_OAUTH_TOKEN\s*=\s*[\"']?([^\s\"'#]+)", re.IGNORECASE),
        True,
    ),
    (
        "OAuth JSON value",
        re.compile(r"[\"'](?:oauth|oauth_token)[\"']\s*:\s*[\"']([^\"']+)", re.IGNORECASE),
        True,
    ),
)


def is_local_excluded(path: Path, root: Path) -> bool:
    relative_parts = path.relative_to(root).parts[:-1]
    return any(
        part in LOCAL_EXCLUDED_DIRECTORIES or part.startswith("build")
        for part in relative_parts
    )


def tracked_paths(root: Path) -> set[Path]:
    if not (root / ".git").exists():
        return set()
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"],
        check=True,
        capture_output=True,
    )
    return {
        (root / entry.decode("utf-8")).resolve()
        for entry in result.stdout.split(b"\0")
        if entry
    }


def candidate_paths(root: Path) -> dict[Path, bool]:
    candidates = {
        path.resolve(): False
        for path in root.rglob("*")
        if path.is_file() and not is_local_excluded(path, root)
    }
    for path in tracked_paths(root):
        candidates[path] = True
    return candidates


def audit(root: Path) -> list[str]:
    root = root.resolve()
    findings: list[str] = []

    for path, is_tracked in sorted(
        candidate_paths(root).items(), key=lambda item: item[0].as_posix()
    ):
        relative_path = path.relative_to(root)
        relative = relative_path.as_posix()
        directory_parts = relative_path.parts[:-1]
        in_forbidden_directory = any(
            part in FORBIDDEN_TRACKED_DIRECTORIES or part.startswith("build")
            for part in directory_parts
        )
        if in_forbidden_directory:
            if is_tracked or not any(part.startswith("build") for part in directory_parts):
                findings.append(f"{relative}: forbidden generated/private directory")
            continue
        if path.suffix.lower() in GENERATED_SUFFIXES:
            findings.append(f"{relative}: generated/private artifact")
            continue
        if path.name == ".env" or (
            path.name.startswith(".env.") and path.name != ".env.example"
        ):
            findings.append(f"{relative}: private environment file")
            continue

        data = path.read_bytes()
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            findings.append(f"{relative}: unreviewed binary content")
            continue
        if b"\0" in data:
            findings.append(f"{relative}: unreviewed binary content")
            continue

        for line_number, line in enumerate(text.splitlines(), start=1):
            for label, pattern, allows_placeholder in SECRET_PATTERNS:
                for match in pattern.finditer(line):
                    value = match.group(1) if allows_placeholder else match.group(0)
                    if allows_placeholder and PLACEHOLDER_PATTERN.fullmatch(value):
                        continue
                    findings.append(f"{relative}:{line_number}: {label}")

    return findings


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    findings = audit(root)
    if findings:
        print("Release-safety audit failed; values are intentionally redacted:", file=sys.stderr)
        for finding in findings:
            print(f"- {finding}", file=sys.stderr)
        return 1

    print("Release-safety audit passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
