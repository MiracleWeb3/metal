#!/usr/bin/env python3
"""Transitional shim. The hook is C++ now (split.cpp); this exists only so a session
that cached the previous hooks.json — which invokes `python3 split.py` — does not error
on a missing file. A PreToolUse hook that errors BLOCKS the write, so deleting this
outright would break writes for anyone who updates metal mid-session.

Delete after one release. It holds no policy: it builds the binary if needed and hands
over, and fails open if anything goes wrong.
"""
import os
import subprocess
import sys

root = os.path.dirname(os.path.abspath(__file__))
binary = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"),
                      "metal", "split")
try:
    src = os.path.join(root, "split.cpp")
    if not os.path.exists(binary) or os.path.getmtime(src) > os.path.getmtime(binary):
        os.makedirs(os.path.dirname(binary), exist_ok=True)
        subprocess.run(["c++", "-std=c++20", "-O2", "-o", binary, src], check=True,
                       stderr=subprocess.DEVNULL)
    os.execv(binary, [binary])
except Exception:
    sys.exit(0)  # fail open: a broken hook must never take the edit with it
