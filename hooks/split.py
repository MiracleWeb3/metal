#!/usr/bin/env python3
"""metal: refuse oversized source files.

Wired to PreToolUse(Write) — denies the write outright — and PostToolUse(Write|Edit)
— warns when a file is over the limit, or approaching it. Self-check: ./split.py --selftest

Two thresholds, because one only ever spoke at failure. Measured across 109 authored
C/C++ files: median 78, p90 202, max 299, nothing over. That last number is the tell —
files were being squeezed under the ceiling (299, 296, 289) rather than split, because
nothing said anything until the deny landed at 301, which is the worst moment to go
looking for a seam. WARN fires at 220 on 8% of those files, while there is still slack.

The override exists because "never exceed 300" and "never cut at an arbitrary line"
contradict each other for a genuinely cohesive unit — a lexer, a transition table, a
generated parser. Without a way to say yes, the only legal move on such a file is a bad
split. The marker makes the exception cost something: a number and a written reason,
sitting in the file, visible in review.
"""
import json
import os
import re
import sys

LIMIT = 300  # keep in sync with SKILL.md
WARN = 220  # advisory only; ~8% of real files, all with runway left
MAX_OVERRIDE = 2 * LIMIT  # a bounded exception is an exception; an unbounded one is a repeal
MIN_REASON = 20  # "because" is not a reason

# // metal: allow 380 — one lexer state machine; the transition table does not split
OVERRIDE = re.compile(r"""(?:\#|//|/\*|--)\s*metal:\s*allow\s+(\d+)\s*[-—:]+\s*(\S.*)""")

CODE = {".rs", ".c", ".h", ".cpp", ".hpp", ".cc", ".zig", ".go", ".py", ".js", ".jsx",
        ".ts", ".tsx", ".vue", ".svelte", ".java", ".rb", ".php", ".sh", ".swift",
        ".kt", ".cs", ".lua", ".ex", ".exs", ".sql", ".asm", ".s"}
SKIP = {"node_modules", "target", "vendor", "dist", "build", ".git"}


def override_of(text):
    """(allowance, reason) if the file grants itself one, else None.

    Rejects a bare number, a throwaway reason, and anything past MAX_OVERRIDE — an
    exception that can name any size is not an exception.
    """
    m = OVERRIDE.search(text or "")
    if not m:
        return None
    allowance, reason = int(m.group(1)), m.group(2).strip().rstrip("*/").strip()
    if allowance > MAX_OVERRIDE or len(reason) < MIN_REASON:
        return None
    return allowance, reason


def advice(path, n):
    return (f"{os.path.basename(path)} is {n} lines; the limit is {LIMIT}. Split it now, "
            f"before anything else.\nMake a directory named after the file, give each concern "
            f"its own file, re-export from one entry point (Rust mod.rs, C header, TS index). "
            f"Cut along seams that already exist - parse/emit/state/io - not at an arbitrary line."
            f"\nIf this file genuinely does not split, say so in it and why:\n"
            f"  // metal: allow {min(n + 40, MAX_OVERRIDE)} - <what makes it one unit>")


def nudge(path, n):
    return (f"{os.path.basename(path)} is {n} lines, {LIMIT - n} from the limit. Find the seam "
            f"now while there is still slack - parse/emit/state/io. Splitting at 301 means "
            f"cutting wherever you happen to be, which is how one coherent file becomes two "
            f"incoherent ones.")


def check(ev):
    """Return the hook's stdout payload, or None to stay quiet."""
    ti = ev.get("tool_input") or {}
    path = ti.get("file_path") or ""
    if os.path.splitext(path)[1].lower() not in CODE or SKIP & set(path.split("/")):
        return None

    pre = ev.get("hook_event_name") == "PreToolUse"
    if pre:
        text = ti.get("content") or ""
    elif os.path.isfile(path):
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    else:
        return None
    n = len(text.splitlines())

    grant = override_of(text)
    if grant and n <= grant[0]:
        return None
    if n <= WARN:
        return None

    if n > LIMIT:
        if pre:
            return {"hookSpecificOutput": {"hookEventName": "PreToolUse",
                                           "permissionDecision": "deny",
                                           "permissionDecisionReason": advice(path, n)}}
        # Creating an oversized file is denied outright; editing one that is already
        # oversized only warns, so a one-line fix to legacy code is not held hostage.
        return {"hookSpecificOutput": {"hookEventName": "PostToolUse",
                                       "additionalContext": advice(path, n)}}

    # In the warn band. Advisory on both paths: a fresh 250-line file is the cheapest
    # possible moment to hear it, and PreToolUse can allow while still saying something.
    if pre:
        return {"hookSpecificOutput": {"hookEventName": "PreToolUse",
                                       "permissionDecision": "allow",
                                       "permissionDecisionReason": nudge(path, n)}}
    return {"hookSpecificOutput": {"hookEventName": "PostToolUse",
                                   "additionalContext": nudge(path, n)}}


def selftest():
    import tempfile
    big, mid, small = "x\n" * 400, "x\n" * 250, "x\n" * 10
    pre = lambda p, c: check({"hook_event_name": "PreToolUse",
                              "tool_input": {"file_path": p, "content": c}})

    # the hard limit, unchanged
    assert pre("/p/a.rs", big)["hookSpecificOutput"]["permissionDecision"] == "deny"
    assert pre("/p/a.rs", small) is None
    assert pre("/p/a.md", big) is None, "docs and data are not source"
    assert pre("/p/target/a.rs", big) is None, "build output is not ours"
    assert pre("", big) is None

    # the warn band allows, but says so, and says something different from the deny
    warn = pre("/p/a.rs", mid)["hookSpecificOutput"]
    assert warn["permissionDecision"] == "allow", "a warn that blocks is just a lower limit"
    assert "50 from the limit" in warn["permissionDecisionReason"]
    assert warn["permissionDecisionReason"] != advice("/p/a.rs", 250), "warn != deny"
    assert pre("/p/a.rs", "x\n" * (WARN - 1)) is None, "quiet below the warn"

    # the override: granted, bounded, and it must cost a reason
    ok_mark = "// metal: allow 380 - one lexer state machine, the table does not split\n"
    assert pre("/p/lex.rs", ok_mark + "x\n" * 350) is None, "a justified exception is allowed"
    assert pre("/p/lex.rs", ok_mark + "x\n" * 400)["hookSpecificOutput"][
        "permissionDecision"] == "deny", "past its own allowance is still denied"
    assert pre("/p/lex.rs", "// metal: allow 380 - meh\n" + "x\n" * 350)["hookSpecificOutput"][
        "permissionDecision"] == "deny", "a throwaway reason is not a reason"
    assert pre("/p/lex.rs", "// metal: allow 9000 - " + "y" * 40 + "\n" + "x\n" * 350)[
        "hookSpecificOutput"]["permissionDecision"] == "deny", "an unbounded exception is a repeal"
    assert pre("/p/lex.py", "# metal: allow 380 - one lexer state machine, does not split\n"
               + "x\n" * 350) is None, "comment syntax follows the language"

    with tempfile.NamedTemporaryFile("w", suffix=".rs", delete=False) as t:
        t.write(big)
    post = lambda p: check({"hook_event_name": "PostToolUse", "tool_input": {"file_path": p}})
    assert post(t.name)["hookSpecificOutput"]["additionalContext"].startswith(
        os.path.basename(t.name))
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
