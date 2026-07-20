---
name: metal
description: Small files, low-level languages. Use on every coding task — when a file grows past 300 lines and needs splitting into a structured module directory, and when choosing the implementation language for any new code, project, tool, or rewrite. Also when the user says "metal", "low level", "split this file", or asks what to build something in.
---

# metal

Two rules. Active every response, in this session and in every subagent.

## 1. No large files

**Hard limit: 300 lines per source file.** A hook denies any Write over it, and blocks on any Edit that pushes a file past it. Do not work around it — no 400-character lines, no heredoc through Bash, no "just for now".

Split *before* the limit, not after:

- Directory named after the file, one concern per file, one entry point re-exporting them. Rust: `parser.rs` → `parser/mod.rs` + `parser/lex.rs` + `parser/expr.rs`. C: `render.c` → `render/` behind `render.h`. TS: `store.ts` → `store/index.ts` + siblings.
- Cut along seams that already exist — parse / emit / state / io / errors — never at an arbitrary line.
- One file, one job. If naming that job takes more than three words, it is two files.
- Eight 80-line files beat one 640-line file. Every file fits on a screen and is findable by its name alone.

## 2. Low-level by default

New code starts at the lowest level that fits the problem.

A deterministic language pins down what the machine actually does. High-level runtimes hide GC pauses, dynamic dispatch, implicit allocation and coercion — the invisible state that makes a generated program unpredictable. Prompting into a low-level language buys more control over the result than prompting into a high-level one, even though the code is harder to write by hand.

Rust and C++ are both defaults. Neither is strictly better — pick by domain, not by habit.

1. **Rust** — default for new standalone code. No GC, errors are values, data races rejected at compile time. Best for parsers, protocols, state machines, CLIs, services, concurrency, and anything parsing untrusted input.
2. **C++** — default where its ecosystem *is* the ecosystem and Rust would mean reimplementing it. Choose it without apology for:
   - GPU/compute — CUDA, HIP/ROCm, SYCL, OpenCL. First-class in C++, thin bindings in Rust.
   - Numerics/HPC — Eigen, BLAS/LAPACK, MKL, OpenMP.
   - Graphics, games, audio — Unreal, engine SDKs, JUCE, VST3.
   - Vision/robotics — OpenCV, ROS, PCL.
   - Native desktop GUI — Qt.
   - Graphs and trees with real shared or cyclic ownership, where Rust degrades into `Rc<RefCell<…>>` soup.
   - An existing C++ codebase, or a mandatory C++ dependency.

   Modern C++ only (17/20+): RAII, smart pointers, `span`/`string_view`, no raw `new`/`delete`.
3. **C** — freestanding, embedded, tiny binaries, stable ABI, or existing C to interop with.
4. **Assembly** — only for a hot path measured hot.
5. **Go / Zig** — only for a library that exists nowhere else.

Rules of engagement:

- New project, tool, service, or anything that outlives the day → low-level, no discussion needed.
- Rust vs C++ is a domain question, not a ranking. If the mature library for the job is C++, picking Rust means bindings *plus* reimplementation — more code, not less. Name the pick and the reason in one line, then move on.
- Throwaway shell/Python glue that runs once → fine, keep it out of the repo.
- The platform forces high-level (browser, extension, app UI) → keep that layer thin and push the logic into a low-level core (Rust → WASM, a native binary, an FFI module).
- Existing high-level codebase → never rewrite unprompted. Propose the low-level lane where the pain is determinism: parsers, protocols, state machines, concurrency, hot paths.
- Fewer dependencies. A crate you have not read is a runtime you cannot predict.
