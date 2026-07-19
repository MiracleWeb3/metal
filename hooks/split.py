#!/usr/bin/env python3
"""metal: refuse oversized source files.

Wired to PreToolUse(Write) — denies the write outright — and PostToolUse(Write|Edit)
— blocks when an edit pushed a file past the limit. Self-check: ./split.py --selftest
"""
import json
import os
import sys

LIMIT = 300  # keep in sync with SKILL.md
CODE = {".rs", ".c", ".h", ".cpp", ".hpp", ".cc", ".zig", ".go", ".py", ".js", ".jsx",
        ".ts", ".tsx", ".vue", ".svelte", ".java", ".rb", ".php", ".sh", ".swift",
        ".kt", ".cs", ".lua", ".ex", ".exs", ".sql", ".asm", ".s"}
SKIP = {"node_modules", "target", "vendor", "dist", "build", ".git"}


def advice(path, n):
    return (f"{os.path.basename(path)} is {n} lines; the limit is {LIMIT}. Split it now, "
            f"before anything else.\nMake a directory named after the file, give each concern "
            f"its own file, re-export from one entry point (Rust mod.rs, C header, TS index). "
            f"Cut along seams that already exist - parse/emit/state/io - not at an arbitrary line.")


def check(ev):
    """Return the hook's stdout payload, or None to stay quiet."""
    ti = ev.get("tool_input") or {}
    path = ti.get("file_path") or ""
    if os.path.splitext(path)[1].lower() not in CODE or SKIP & set(path.split("/")):
        return None

    pre = ev.get("hook_event_name") == "PreToolUse"
    if pre:
        n = len((ti.get("content") or "").splitlines())
    elif os.path.isfile(path):
        with open(path, "rb") as fh:
            n = sum(1 for _ in fh)
    else:
        return None

    if n <= LIMIT:
        return None
    if pre:
        return {"hookSpecificOutput": {"hookEventName": "PreToolUse",
                                       "permissionDecision": "deny",
                                       "permissionDecisionReason": advice(path, n)}}
    # Creating an oversized file is denied outright; editing one that is already
    # oversized only warns, so a one-line fix to legacy code is not held hostage.
    return {"hookSpecificOutput": {"hookEventName": "PostToolUse",
                                   "additionalContext": advice(path, n)}}


def selftest():
    import tempfile
    big, small = "x\n" * 400, "x\n" * 10
    pre = lambda p, c: check({"hook_event_name": "PreToolUse",
                              "tool_input": {"file_path": p, "content": c}})
    assert pre("/p/a.rs", big)["hookSpecificOutput"]["permissionDecision"] == "deny"
    assert pre("/p/a.rs", small) is None
    assert pre("/p/a.md", big) is None, "docs and data are not source"
    assert pre("/p/target/a.rs", big) is None, "build output is not ours"
    assert pre("", big) is None

    with tempfile.NamedTemporaryFile("w", suffix=".rs", delete=False) as t:
        t.write(big)
    post = lambda p: check({"hook_event_name": "PostToolUse", "tool_input": {"file_path": p}})
    assert post(t.name)["hookSpecificOutput"]["additionalContext"].startswith(os.path.basename(t.name))
    os.unlink(t.name)
    assert post(__file__) is None, "this file must obey its own rule"
    assert post("/p/gone.rs") is None
    print("ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        out = check(json.load(sys.stdin))
        if out:
            print(json.dumps(out))
