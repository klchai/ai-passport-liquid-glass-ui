#!/usr/bin/env python3
"""Dependency-free repository checks shared by local development and CI."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parent.parent
FULL_SHA_RE = re.compile(r"^[0-9a-f]{40}$")
MARKDOWN_LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
CJK_RE = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
SECRET_PATTERNS = {
    "GitHub token": re.compile(r"(?:ghp_|github_pat_)[A-Za-z0-9_]{20,}"),
    "AWS access key": re.compile(r"AKIA[0-9A-Z]{16}"),
    "private key": re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
}
RADIUS_CALL_RE = re.compile(r"\b(solid_object|reference_glass_create)\s*\(")
ALLOWED_RADIUS_ARGS = {
    "0",
    "LV_RADIUS_CIRCLE",
    "UI_GLASS_RADIUS_CONTROL",
    "UI_GLASS_RADIUS_PANEL",
    "UI_GLASS_RADIUS_FLOATING",
}
COMPACT_RADIUS_DEFINITIONS = {
    "SHOWCASE_RADIUS_CHECKBOX_OUTER": "6",
    "SHOWCASE_RADIUS_CHECKBOX_INNER": "3",
}
COMPACT_RADIUS_CALLS = {
    "SHOWCASE_RADIUS_CHECKBOX_OUTER": (
        "component.root", "14", "10", "22", "22",
    ),
    "SHOWCASE_RADIUS_CHECKBOX_INNER": (
        "mark", "5", "5", "12", "12",
    ),
}


def git_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return [ROOT / line for line in result.stdout.splitlines() if line]


def text_files() -> list[Path]:
    files: list[Path] = []
    for path in git_files():
        if not path.is_file() or path.stat().st_size > 2 * 1024 * 1024:
            continue
        try:
            path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        files.append(path)
    return files


def check_required_files(errors: list[str]) -> None:
    required = (
        "AGENTS.md",
        "AGENTS.zh_CN.md",
        "CLAUDE.md",
        "CLAUDE.zh_CN.md",
        "docs/CHANGELOG.md",
        ".github/CONTRIBUTING.md",
        ".github/CODE_OF_CONDUCT.md",
        ".github/SECURITY.md",
        ".github/SUPPORT.md",
        "dependencies.lock",
        "sdkconfig.defaults",
        "partitions.csv",
        ".github/PULL_REQUEST_TEMPLATE.md",
    )
    for name in required:
        if not (ROOT / name).is_file():
            errors.append(f"missing required file: {name}")

    ignored = subprocess.run(
        ["git", "check-ignore", "-q", "dependencies.lock"], cwd=ROOT
    )
    if ignored.returncode == 0:
        errors.append("dependencies.lock must be tracked, not ignored")


def check_markdown_links(files: list[Path], errors: list[str]) -> None:
    for path in files:
        if path.suffix.lower() != ".md":
            continue
        text = path.read_text(encoding="utf-8")
        for raw_target in MARKDOWN_LINK_RE.findall(text):
            target = raw_target.strip().split(maxsplit=1)[0].strip("<>")
            if not target or target.startswith(("#", "http://", "https://", "mailto:")):
                continue
            local = unquote(target.split("#", 1)[0])
            resolved = (ROOT / local.lstrip("/")) if local.startswith("/") else (path.parent / local)
            if local and not resolved.resolve().exists():
                errors.append(f"{path.relative_to(ROOT)}: missing link target {target}")


def check_document_languages(files: list[Path], errors: list[str]) -> None:
    """Require an English default and a linked Simplified Chinese peer."""
    markdown = {path.resolve() for path in files if path.suffix.lower() == ".md"}

    for path in sorted(markdown):
        name = path.name
        text = path.read_text(encoding="utf-8")
        opening = "\n".join(text.splitlines()[:8])

        if name.endswith(".zh_CN.md"):
            default_name = f"{name[:-len('.zh_CN.md')]}.md"
            default_path = path.with_name(default_name).resolve()
            if default_path not in markdown:
                errors.append(
                    f"{path.relative_to(ROOT)}: missing English default {default_name}"
                )
            elif default_name not in opening:
                errors.append(
                    f"{path.relative_to(ROOT)}: missing top language link to {default_name}"
                )
            continue

        chinese_name = f"{path.stem}.zh_CN.md"
        chinese_path = path.with_name(chinese_name).resolve()
        if chinese_path not in markdown:
            errors.append(
                f"{path.relative_to(ROOT)}: missing Simplified Chinese peer {chinese_name}"
            )
        elif chinese_name not in opening:
            errors.append(
                f"{path.relative_to(ROOT)}: missing top language link to {chinese_name}"
            )

        english_prose = text.replace("简体中文", "")
        match = CJK_RE.search(english_prose)
        if match:
            line = english_prose.count("\n", 0, match.start()) + 1
            errors.append(
                f"{path.relative_to(ROOT)}:{line}: default Markdown must use English prose"
            )


def check_action_pins(errors: list[str]) -> None:
    workflow_dir = ROOT / ".github" / "workflows"
    for path in sorted(workflow_dir.glob("*.y*ml")):
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = re.match(r"\s*-?\s*uses:\s*['\"]?([^'\"\s]+)", line)
            if not match:
                continue
            action = match.group(1)
            if action.startswith("./"):
                continue
            if action.startswith("docker://"):
                if "@sha256:" not in action:
                    errors.append(f"{path.relative_to(ROOT)}:{line_number}: unpinned Docker action {action}")
                continue
            if "@" not in action or not FULL_SHA_RE.fullmatch(action.rsplit("@", 1)[1]):
                errors.append(f"{path.relative_to(ROOT)}:{line_number}: action must use a full commit SHA: {action}")


def check_issue_forms(errors: list[str]) -> None:
    issue_dir = ROOT / ".github" / "ISSUE_TEMPLATE"
    for name in ("feature_request.yml", "usage_question.yml"):
        path = issue_dir / name
        if not path.is_file():
            errors.append(f"missing issue form: {path.relative_to(ROOT)}")
            continue
        text = path.read_text(encoding="utf-8")
        for field in ("name:", "description:", "body:"):
            if not re.search(rf"(?m)^{re.escape(field)}", text):
                errors.append(f"{path.relative_to(ROOT)}: missing top-level {field[:-1]}")


def check_sensitive_content(files: list[Path], errors: list[str]) -> None:
    for path in files:
        text = path.read_text(encoding="utf-8")
        for label, pattern in SECRET_PATTERNS.items():
            if pattern.search(text):
                errors.append(f"{path.relative_to(ROOT)}: possible {label}")

        for match in re.finditer(r"https://ai-passport\.folotoy\.cn/trae/\?s=([^&\s)]+)&k=([^\s)]+)", text):
            if "<" not in match.group(1) and "AAAAAA" not in match.group(1):
                errors.append(f"{path.relative_to(ROOT)}: possible unsanitized device QR link")


def check_conflict_markers(files: list[Path], errors: list[str]) -> None:
    marker = re.compile(r"(?m)^(<<<<<<< |=======\s*$|>>>>>>> )")
    for path in files:
        if marker.search(path.read_text(encoding="utf-8")):
            errors.append(f"{path.relative_to(ROOT)}: unresolved merge conflict marker")


def split_call_args(source: str) -> list[str]:
    args: list[str] = []
    current: list[str] = []
    depth = 0
    quote = None
    escape = False

    for char in source:
        if quote:
            current.append(char)
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = None
            continue

        if char in ('"', "'"):
            quote = char
            current.append(char)
        elif char == "(":
            depth += 1
            current.append(char)
        elif char == ")":
            depth -= 1
            current.append(char)
        elif char == "," and depth == 0:
            args.append(" ".join("".join(current).split()))
            current = []
        else:
            current.append(char)

    if current or source.strip():
        args.append(" ".join("".join(current).split()))
    return args


def mask_c_comments_and_literals(source: str) -> str:
    """Replace C comments and quoted literals with spaces, preserving offsets."""
    masked = list(source)
    index = 0
    while index < len(source):
        if source.startswith("//", index):
            end = source.find("\n", index + 2)
            if end < 0:
                end = len(source)
            for position in range(index, end):
                masked[position] = " "
            index = end
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            end = len(source) if end < 0 else end + 2
            for position in range(index, end):
                if source[position] != "\n":
                    masked[position] = " "
            index = end
            continue
        if source[index] in ('"', "'"):
            quote = source[index]
            masked[index] = " "
            index += 1
            escape = False
            while index < len(source):
                char = source[index]
                if char != "\n":
                    masked[index] = " "
                index += 1
                if escape:
                    escape = False
                elif char == "\\":
                    escape = True
                elif char == quote:
                    break
            continue
        index += 1
    return "".join(masked)


def find_matching_paren(source: str, open_index: int) -> int:
    depth = 0
    quote = None
    escape = False
    index = open_index
    while index < len(source):
        char = source[index]
        if quote:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = None
        elif char in ('"', "'"):
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return -1


def allowed_radius_arg(argument: str) -> bool:
    return argument in ALLOWED_RADIUS_ARGS


def allowed_compact_radius_call(path: Path, args: list[str]) -> bool:
    if path.name != "showcase_scenes.c" or len(args) < 6:
        return False
    expected = COMPACT_RADIUS_CALLS.get(args[5])
    return expected is not None and tuple(args[:5]) == expected


def check_radius_tokens(errors: list[str]) -> None:
    """Keep showcase rounded rectangles on the design-system radius tokens."""
    radius_arg_index = {
        "solid_object": 5,
        "reference_glass_create": 5,
    }
    compact_uses = {name: 0 for name in COMPACT_RADIUS_CALLS}
    for path in sorted((ROOT / "main").glob("*.c")):
        text = path.read_text(encoding="utf-8")
        code = mask_c_comments_and_literals(text)
        if path.name == "showcase_scenes.c":
            for name, expected in COMPACT_RADIUS_DEFINITIONS.items():
                definition = re.search(
                    rf"(?m)^\s*#define\s+{re.escape(name)}\s+(\S+)", code
                )
                if not definition or definition.group(1) != expected:
                    errors.append(
                        f"{path.relative_to(ROOT)}: {name} must remain {expected} "
                        "until a compact design token is approved"
                    )
        for match in RADIUS_CALL_RE.finditer(code):
            function = match.group(1)
            open_index = match.end() - 1
            close_index = find_matching_paren(code, open_index)
            if close_index < 0:
                line = text.count("\n", 0, match.start()) + 1
                errors.append(
                    f"{path.relative_to(ROOT)}:{line}: unterminated "
                    f"{function} call"
                )
                continue
            # Skip helper definitions; the next non-space token is their body.
            if code[close_index + 1 :].lstrip().startswith("{"):
                continue
            args = split_call_args(code[open_index + 1 : close_index])
            arg_index = radius_arg_index[function]
            if len(args) <= arg_index:
                continue
            radius = args[arg_index]
            compact_exception = allowed_compact_radius_call(path, args)
            if compact_exception:
                compact_uses[radius] += 1
            if not (allowed_radius_arg(radius) or compact_exception):
                line = text.count("\n", 0, match.start()) + 1
                errors.append(
                    f"{path.relative_to(ROOT)}:{line}: {function} radius must "
                    "use UI_GLASS_RADIUS_*, LV_RADIUS_CIRCLE, 0, or the "
                    "locked compact-checkbox exception "
                    f"(got {radius})"
                )
    for name, count in compact_uses.items():
        if count != 1:
            errors.append(
                f"main/showcase_scenes.c: {name} must be used by exactly one "
                f"locked checkbox call (found {count})"
            )


def main() -> int:
    errors: list[str] = []
    files = text_files()
    check_required_files(errors)
    check_markdown_links(files, errors)
    check_document_languages(files, errors)
    check_action_pins(errors)
    check_issue_forms(errors)
    check_sensitive_content(files, errors)
    check_conflict_markers(files, errors)
    check_radius_tokens(errors)

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(f"Repository checks: PASS ({len(files)} text files scanned)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
