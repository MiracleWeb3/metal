# metal rule 3 — the compiler refuses

**Date:** 2026-08-01
**Status:** approved, not yet implemented

## Problem

metal enforces one property of the code it governs: file size. That rule works — measured
across 690 real writes on this machine, split at metal's install date (2026-07-19):

| | before (n=106) | after (n=584) |
|:--|--:|--:|
| median | 63 | 68 |
| p90 | 245 | 170 |
| p99 | 605 | 277 |
| max | 679 | 334 |

The effect survives the obvious confound: restricted to languages rule 2 never applied to
(non-C/C++, n=104 → 308), p90 still falls 245 → 148 and max 679 → 334. The median does not
move, which is the intended shape — the rule was never meant to change typical files, only
to kill the monsters.

That benefit has already landed. Genuine (non-test) over-300 write attempts in the 13 days
after install: **one**. Rule 1 has almost nothing left to catch here.

Meanwhile the highest-value quality lever on this machine is real, is already being
practised, and is enforced by nothing:

| project | build floor | |
|:--|:--|:--|
| parserx | 15/15 modules `-Wall -Wextra -Werror`, 5 with asan | exemplary |
| claude-memory-light | `-Wall -Wextra -Wpedantic` — no `-Werror`, no asan | **drifted** |
| skill-matcher | no build file at all | never had one |
| metal itself | `c++ -std=c++20 -O2 … 2>/dev/null` | **no warnings, stderr swallowed** |

Across all transcripts: 433 build invocations, 431 uses of `-Werror`, 47 of
`-fsanitize=address,undefined`. The discipline is genuine. But it lives in hand-written
per-project build files, so it drifts the moment attention moves — and it has already
drifted twice.

`SKILL.md` states the gap outright:

> *"Modern C++ only (17/20+): RAII, smart pointers, `span`/`string_view`, no raw
> `new`/`delete`. **Discipline comes from style, not from the compiler refusing.**"*

That sentence concedes exactly the fight metal was built to win. metal's thesis is that a
rule living in prose gets ignored, so it moves into a hook where it cannot be. The compiler
flags are the most valuable rule here and the least protected — they are not even in prose,
they are in muscle memory.

**Rule 3 makes the compiler the enforcer.** It is the first thing in metal that is a payoff
for choosing C++ rather than a restriction following from it.

## The floor

```
release:  -std=c++20 -Wall -Wextra -Werror
debug:    -fsanitize=address,undefined -D_GLIBCXX_ASSERTIONS -O1 -g
```

`c++20` is a minimum, not an equality — `c++23` and later satisfy it (parserx already uses
both). A project satisfies the floor when its governing build file carries `-Wall`,
`-Wextra`, and `-Werror`. A sanitizer mode is recommended and reported when absent, but its
absence alone does not constitute a violation.

## Components

### 1. `SKILL.md` — declare the floor

Replace the "discipline comes from style" sentence with the floor above, stated as a rule.
Net line change must be ≈0: `SKILL.md` is injected at every `SessionStart` *and*
`SubagentStart`, so its length is a per-session token cost paid on every conversation.

### 2. `hooks/floor.cpp` — the check

**Trigger:** `PostToolUse(Write|Edit)` on `.c .h .cpp .hpp .cc .hh .cxx`.

Advisory, never a deny. Denying a source write because a *build file* is wrong punishes the
wrong artifact, and `PostToolUse` already fires for both Write and Edit.

**Algorithm**

1. Skip unless the written file is C/C++.
2. Skip if the path is under `/tmp/` or contains `/scratchpad/`. This is metal's own escape
   (a) — one-shot glue that never enters a repo — made mechanical. Measurement showed 139 of
   207 candidate firings for an earlier proposal were scratchpad noise; applying the lesson
   up front is what keeps this check credible.
3. Skip if `in_skipped_dir()` matches (`node_modules`, `target`, `vendor`, `dist`, `build`,
   `.git`) — the existing helper.
4. Walk up from the file's directory looking for the governing build file, in this order at
   each level: `build.sh`, `CMakeLists.txt`, `Makefile`, `makefile`, `meson.build`. Stop at
   the first hit, at a directory containing `.git`, or after 8 levels.
5. **Found:** read it; check for `-Wall`, `-Wextra`, `-Werror` as substrings. Report only
   the flags that are missing, naming the build file by repo-relative path. Note separately
   when no `fsanitize` appears anywhere in it.
6. **Not found:** report that the project has no build file, once.
7. All three present → silent.

**Throttle.** Without one, writing twelve files into a project emits twelve identical
warnings and the check becomes wallpaper. Stamp file at
`${XDG_CACHE_HOME:-$HOME/.cache}/metal/seen/<hash-of-build-file-path>`, containing the build
file's mtime. Skip when the stamp exists and the recorded mtime still matches. This
re-fires precisely when the build file changes — i.e. when the thing being judged has moved.
For the not-found case, key the stamp on the walk's stopping directory.

### 3. `hooks/common.hpp` — shared helpers

Move `lower()`, `basename_of()`, `is_source()`, `in_skipped_dir()`, and `payload()` out of
`split.cpp`. `split.cpp` is at 240 lines and rule 3 adds ~60; the move drops rule 1 back to
roughly 180 and leaves both files with runway. Two new files, not a directory restructure —
the seam is rule 1 versus rule 3, and it already exists.

Resulting layout:

```
hooks/hookjson.hpp   unchanged
hooks/common.hpp     shared helpers          (new)
hooks/split.cpp      rule 1 + main dispatch
hooks/floor.cpp      rule 3                  (new)
hooks/selftest.inc   extended
```

### 4. `hooks/hooks.json` — metal stops hiding its own failures

Current:

```sh
c++ -std=c++20 -O2 -o "$B" "$S/split.cpp" 2>/dev/null \
  || echo "metal: no working C++ compiler on PATH, so the 300-line hook is inert this session"
```

Two defects. It builds metal without the floor metal is about to require of everyone else.
And `2>/dev/null` discards the compiler's diagnosis, so any build failure is reported as
"no working C++ compiler" — a misdiagnosis. On 2026-07-26 this let the hook run against a
deleted `split.py` path, fail open, and stay silently dead; a 310-line write sailed through
and neither party noticed for six days.

Replacement:

```sh
c++ -std=c++20 -O2 -Wall -Wextra -Werror -o "$B" "$S"/*.cpp 2>&1 \
  || echo "metal: hook failed to build (see above) — BOTH RULES ARE INERT this session"
```

Compiler output is shown rather than swallowed, the message names the consequence instead of
guessing the cause, and metal compiles itself under the floor. The staleness check
(`ls -t "$S"/*.cpp "$S"/*.hpp "$S"/*.inc`) already globs the hooks directory and covers the
new files unchanged.

The `PreToolUse`/`PostToolUse` commands keep `[ -x "$B" ] && exec "$B" || exit 0`. Failing
open at the tool call is correct — a dead hook must not take the edit with it. Visibility
belongs at `SessionStart`, where a human reads it.

This change is low-risk rather than speculative: `.github/workflows/test.yml:15` already
builds with `-std=c++20 -O2 -Wall -Wextra -Werror`, so `split.cpp` is known to compile clean
under the floor. Only the runtime build has been lagging CI.

### 5. `.github/workflows/test.yml` — stop hardcoding the file list

The workflow names `hooks/split.cpp` explicitly at lines 15, 19, and 27, and line 27's
line-limit loop iterates a hardcoded list of three files. Adding `floor.cpp` and
`common.hpp` breaks the first two and — worse — silently escapes the third: metal's own
files would stop being checked against metal's own rule, and nothing would say so.

- Lines 15 and 19: compile `hooks/*.cpp` instead of `hooks/split.cpp`.
- Line 27: glob `hooks/*.cpp hooks/*.hpp hooks/*.inc` instead of the fixed list, so any file
  added later is covered without anyone remembering to add it.

## Testing

Extend `hooks/selftest.inc`; CI already runs `--selftest`. One case per branch:

- build file with all three flags → silent
- `CMakeLists.txt` with `-Wall -Wextra`, no `-Werror` → warns, names `-Werror` only
- build file with the floor but no `fsanitize` → notes the sanitizer separately
- no build file anywhere up the walk → warns once
- path under `/scratchpad/` → silent
- path under `/tmp/` → silent
- non-C++ file (`.py`, `.ts`) → silent
- stamp present, build-file mtime unchanged → silent
- stamp present, build-file mtime changed → warns again
- all existing rule 1 cases → still pass

The two real-world cases must be reproduced as fixtures: `cml/cpp/CMakeLists.txt`
(`-Wall -Wextra -Wpedantic`, no `-Werror`) must warn, and every `parserx/cpp/*/build.sh`
must stay silent.

## Out of scope

Explicitly not in this change, and why:

- **The language gate** (deny non-C++ tier-1 files). Measured at ≈0 genuine firings after
  its own escape hatch — 42 survivors in 13 days, 41 of them inside one existing Python
  codebase that `SKILL.md` already exempts.
- **The tier model / rule 2 rewrite.** Real (nothing shipping is 100% one language; parserx
  is itself 73% C++ / 23% JS-TS), but it is a documentation change with no enforcement
  attached, and it does not improve code quality on this machine.
- **Public-product repackaging** — C++ as opt-in, `metal stats`, seam-proposing denials.
  Aimed at strangers, not at the quality of code produced here.
- **clang-tidy and the stricter floor** (`-Wconversion`, `-Wshadow`, `-Wold-style-cast`,
  `cppcoreguidelines-*`). Deliberately deferred: it would break builds that pass today, and
  the plain floor should be enforced and holding first.
- **The semantic failure class** — built-but-never-called, connected-but-zero-firings,
  verified-against-a-copy. A write-time hook cannot see any of it. Different instrument,
  separate project.

## Success criteria

1. `cml/cpp/CMakeLists.txt` warns about its missing `-Werror` on the next C++ write into that
   project; all fifteen parserx modules stay silent.
2. metal's own build runs under `-Wall -Wextra -Werror` and prints compiler output on
   failure.
3. A hook that cannot build says so at `SessionStart`, naming both rules as inert.
4. No file in `hooks/` exceeds 300 lines.
5. `--selftest` passes, including the two real-world fixtures.
6. CI builds every file in `hooks/` and line-checks every file in `hooks/`, by glob rather
   than by name, on both ubuntu and macos.

## Known limitation

The flag check is a substring match on the build file's text. It will accept `-Wall`
appearing in a comment and will not understand `-Wno-error` appearing after `-Werror`.
This is deliberate: the check is advisory, it exists to catch a project that never had the
floor rather than one actively subverting it, and a shell/CMake parser is a far larger
object than the problem justifies. If it ever produces a false negative in practice, that
is the signal to reconsider — not before.
