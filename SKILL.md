---
name: metal
description: Small files, low-level languages. Use on every coding task — when a file grows past 300 lines and needs splitting into a structured module directory, and when choosing the implementation language for any new code, project, tool, or rewrite. Also when the user says "metal", "low level", "split this file", or asks what to build something in.
---

# metal

Three rules. Active every response, in this session and in every subagent.

## 1. No large files

**Hard limit: 300 lines per source file.** A hook denies any Write over it, and blocks on any Edit that pushes a file past it. Do not work around it — no 400-character lines, no heredoc through Bash, no "just for now".

**Advisory at 220.** The hook says something while you still have runway. Act on it then: at 301 you cut wherever you happen to be standing, which is how one coherent file becomes two incoherent ones. Measured on a real tree of 109 files, the ones that reached the ceiling had piled up at 289, 296, 299 — squeezed under it rather than split.

**The one legal exception.** A genuinely cohesive unit — a lexer, a transition table, a generated parser — may grant itself room, in the file, with a reason:

```
// metal: allow 380 - one lexer state machine, the transition table does not split
```

Bounded on purpose: at most 600, the reason must be a real sentence, and going past your own number is denied like any other file. "It's long" is not a reason. If you cannot write the sentence, the file splits.

Split *before* the limit, not after:

- Directory named after the file, one concern per file, one entry point re-exporting them. C++: `parser.cpp` → `parser/lex.cpp` + `parser/expr.cpp` + `parser/emit.cpp` behind `parser.hpp`. C: `render.c` → `render/` behind `render.h`.
- Cut along seams that already exist — parse / emit / state / io / errors — never at an arbitrary line.
- One file, one job. If naming that job takes more than three words, it is two files.
- Eight 80-line files beat one 640-line file. Every file fits on a screen and is findable by its name alone.

## 2. Low-level by default

New code starts at the lowest level that fits the problem.

A deterministic language pins down what the machine actually does. High-level runtimes hide GC pauses, dynamic dispatch, implicit allocation and coercion — the invisible state that makes a generated program unpredictable. Prompting into a low-level language buys more control over the result than prompting into a high-level one, even though the code is harder to write by hand.

**C++ by default — for essentially all code.** It is the lowest level we can reach and still work adequately — closest to the machine while keeping a real ecosystem, a real toolchain, and libraries that already exist for almost every domain. There is no task C++ can't solve, and every time we reach for it it has solved what other approaches could not. Start there; step off only when it is 100% ok and safe, and name the reason in one line.

1. **C++ — the default.** Every new project, tool, service, core, or extension module starts here unless something below applies. Direct memory control, no GC, no runtime between the code and the machine, and the mature library for nearly any domain is already C++. It compiles everywhere, binds to everything (pybind11, C ABI, FFI), and nothing is hidden from you.

   Modern C++ only (17/20+): RAII, smart pointers, `span`/`string_view`, no raw `new`/`delete`. Rule 3 is what keeps that honest.

   **Having chosen the level, use it.** C++ written like Python with semicolons pays the whole authorship cost and collects none of the control — that is the only way this choice is actually wrong:

   - **Move work to compile time.** `constexpr` the table instead of building it at startup, `static_assert` the invariant instead of testing it, `[[nodiscard]]` anything returning a status so it cannot be silently dropped. This is the tier no interpreted language has at all, and it is the one most often left on the floor.
   - **Views in, owners out.** `string_view` and `span` at every boundary; take `const std::string&` only when you store it. A `const std::string&` parameter forces an allocation at every call site that had a literal or a view.
   - **Layout is a decision, not a default.** Contiguous over pointer-chasing, `reserve()` before a loop that pushes, struct-of-arrays when you iterate one field across many objects.
   - **Reach for what the level gives** when it is the answer, not for sport: `mmap` over a read loop, a bump allocator over a million small news, SIMD when you measured it, the syscall directly.

   None of this applies to a forty-line tool. Question 2 above still governs.

   Especially where its ecosystem *is* the ecosystem: GPU/compute (CUDA, HIP/ROCm, SYCL, OpenCL), numerics/HPC (Eigen, BLAS/LAPACK, MKL, OpenMP), graphics/games/audio (Unreal, engine SDKs, JUCE, VST3), vision/robotics (OpenCV, ROS, PCL), native GUI (Qt), and graphs or trees with genuine shared or cyclic ownership.

2. **C** — freestanding, embedded, tiny binaries, stable ABI, or existing C to interop with.
3. **Assembly** — only for a hot path measured hot.

**The other rungs exist, and they are not forbidden — they have to be argued for.** Two questions, in order, decide it:

1. **Does it need control a runtime would take away** — memory layout, timing, no GC pause, a hardware or ABI boundary, behaviour identical in a year? → **the lowest rung that fits, C++ first.**
2. **Is it a one-shot you run by hand, or under ~100 lines?** → **the highest rung that fits.** C++ here is excessive work for nothing, and metal is not asking for it. A forty-line script does not need a build system.

When neither question decides it, C++ wins by default — the tie goes to control. What the rest actually buy:

| | when it genuinely wins |
|:--|:--|
| **Rust** | the compiler enforces memory safety; concurrency is the risk and nobody will hold the invariant by hand |
| **Zig** | C's simplicity with modern tooling — production-real (TigerBeetle, Ghostty, Bun), 1.0 in 2026 |
| **Go** | network services and CLIs, where deploy speed beats the last 20% of control |
| **Python** | a script, glue, or where the library *is* the ecosystem — ML, data, scraping |
| **TS/JS** | it has to run in a browser. Anything else there is a choice, not a constraint |

"Safer in general", "better tooling", "the ecosystem is over there" are still preferences, not answers to question 1. And a library that exists elsewhere is *linked* through its C ABI, never rewritten and never joined.

A hook asks this once per project, on the first source file written into a tree that has none — the moment the choice becomes expensive to reverse. Every later file is silent.

Rules of engagement:

- Anything that outlives the day and answers yes to question 1 → C++, no discussion needed.
- **Never silently upgrade the user's stated language.** If they say C++, it is C++ — a "safer equivalent" substituted without asking is a substitution, not an improvement. If a different language genuinely fits better, say so in one line and then do what they asked.
- Stepping off C++ requires that it be 100% ok and safe *and* the reason named in one line. Staying on it requires nothing.
- **An audited implementation that exists in another language gets LINKED, not rewritten and not joined.** A crypto/security protocol with one correct audited implementation (an HSM-backed PIN protocol, a TLS stack, a signature scheme) must not be reimplemented from scratch — but the answer is to call it through its C ABI from C++, not to start writing that language. Same for instrumenting an already-proven path: wrap it, don't re-derive it. Writing C++ and linking a `.so` is still C++ only.
- **The two genuine escapes, both narrow:** (a) one-shot throwaway glue that never enters the repo — a shell one-liner or a five-line script is not a project; (b) a platform that physically forbids native code (the browser, a UI shell) — keep that layer as thin as it can be and push every real decision into a C++ core behind it (native binary, WASM, C ABI). Neither escape is a licence to start a second codebase.
- Existing high-level codebase → never rewrite unprompted. Propose the low-level lane where the pain is determinism: parsers, protocols, state machines, concurrency, hot paths.
- A low-level core that ships but is never built, installed, or called is worth less than the high-level code it replaced. Wire it to a caller and make its absence a hard error, never a silent fallback.
- Fewer dependencies. A library you have not read is a runtime you cannot predict.

## 3. The toolchain refuses

Whatever rung you land on, its strict mode is switched on. Every ecosystem ships one and almost nobody turns it on — and a language chosen for guarantees, run without them, is the worst of both.

| you write | the floor | in |
|:--|:--|:--|
| **C / C++** | `-std=c++20 -Wall -Wextra -Werror`<br>debug: `-fsanitize=address,undefined -D_GLIBCXX_ASSERTIONS` | `build.sh` · `CMakeLists.txt` · `Makefile` · `meson.build` |
| **TypeScript** | `"strict": true` | `tsconfig.json` |
| **Python** | a type checker configured at all — mypy, pyright, pyrefly or ty | `pyproject.toml` · `mypy.ini` · `pyrightconfig.json` |

A hook reads the nearest config when you write, and names what is missing. **`-Werror` is the one that matters — a warning you are allowed to ignore is a bug that compiles**, and `"strict": true` is the identical switch one ecosystem over. `c++20` is a minimum, not an equality.

Setting up a project means writing that config first, with the floor in it. It is not a later step; without it the language has been chosen and its main advantage left switched off.

**Above the floor, escalate rather than hand-roll.** `-Wconversion -Wshadow -Wold-style-cast`, then `clang-tidy` with `performance-*`, `modernize-*`, `cppcoreguidelines-*` — which check the value-parameter copies, the missing `[[nodiscard]]`, the owning raw pointers, mechanically and with full type information. metal does not reimplement that; it is a floor, not a linter. The floor stays at three flags because a floor nobody can adopt is a floor nobody adopts.
