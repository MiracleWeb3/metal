# metal

A Claude Code plugin with two rules: **small files, low-level languages.**

One of them is enforced by a hook, not by asking nicely.

---

## Rule 1 — no large files

**300 lines, hard limit.** A `PreToolUse` hook inspects every `Write` before it happens and **denies** any source file over the limit. The model doesn't get to negotiate; the tool call fails and it is told to split.

```
parser.rs (401 lines)  ->  DENIED
                           parser/mod.rs
                           parser/lex.rs
                           parser/expr.rs
```

Editing a file that is *already* oversized only warns — a one-line fix to legacy code shouldn't be held hostage to a refactor you didn't ask for. Creating new bloat is blocked; inheriting it is not.

Ignores what isn't source: `.md`, `.json`, data files, and anything under `node_modules/`, `target/`, `vendor/`, `dist/`, `build/`.

The split instruction is specific, because "split this file" alone produces garbage:

- A directory named after the file, one concern per file, one entry point re-exporting them
- Cut along seams that already exist — parse / emit / state / io / errors — never at an arbitrary line
- One file, one job. If naming that job takes more than three words, it's two files

## Rule 2 — low-level by default

New code starts at the lowest level that fits the problem. **Rust → C → C++ → assembly**, with high-level languages as the exception that has to justify itself.

The reasoning: a deterministic language pins down what the machine actually does. High-level runtimes hide GC pauses, dynamic dispatch, implicit allocation and coercion — exactly the invisible state that makes a *generated* program unpredictable. Prompting into a low-level language buys more control over the result than prompting into a high-level one, even though the code is harder to write by hand.

This one can't be hooked — language choice happens before any tool call exists to intercept. So `SKILL.md` is injected at every `SessionStart` **and** every `SubagentStart`, which means it survives compaction and reaches delegated subagents that would otherwise default to Python.

It does **not** rewrite your existing codebases. It proposes the low-level lane where the pain is determinism — parsers, protocols, state machines, concurrency, hot paths — and otherwise leaves them alone.

---

## Install

```
/plugin marketplace add MiracleWeb3/metal
/plugin install metal@metal
```

Or clone it into your skills directory, where it loads with no install step at all:

```
git clone https://github.com/MiracleWeb3/metal ~/.claude/skills/metal
```

Restart Claude Code either way — hooks are read at session start.

## Configure

One number, one place:

```python
# hooks/split.py
LIMIT = 300   # keep in sync with SKILL.md
```

Language preference order lives in `SKILL.md` under *Low-level by default*. Both files are meant to be edited; the plugin is small enough to read in full in under two minutes.

## Test

```
python3 hooks/split.py --selftest
```

No framework, no fixtures. Eight assertions covering both hook branches, the extension filter, skipped build directories, and the missing-file path. CI runs it on every push, and checks that `split.py` obeys its own 300-line rule.

## Layout

```
.claude-plugin/plugin.json        manifest
.claude-plugin/marketplace.json   so others can install it
SKILL.md                          both rules, injected every session
hooks/hooks.json                  wiring
hooks/split.py                    the enforcer (~80 lines)
```

## License

MIT
