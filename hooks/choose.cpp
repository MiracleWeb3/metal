// metal: rule 2 — choose the language once, on purpose.
//
// Language choice cannot be hooked in general: it happens in a sentence, before any tool
// call exists to intercept. But it *lands* somewhere — the first source file written into
// a tree that has none — and that write is a tool call. Measured across 30 days of real
// transcripts: 16 such moments, 0.53/day, and 10 of them picked something other than C or
// C++. Rare enough to never read as noise, and it is the exact instant the decision
// becomes expensive to reverse.
//
// It allows, always. A denial here would be the language gate, which was measured and
// discarded: after its own escape hatches it fired ~0 times on real work, and where it did
// fire it was usually wrong. What is missing is not a wall, it is a moment of thought.
//
// The hook supplies the frame; the model supplies the judgement. A hook cannot know
// whether the task is a parser or a loop, so it does not pretend to — it lists what each
// rung actually buys and asks two questions. The second question is the one metal never
// used to ask: a 40-line script written in C++ is excessive work for nothing, and a rule
// that cannot say so is a rule that will be ignored the first time it is obviously wrong.
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "common.hpp"
#include "stamp.hpp"

namespace fs = std::filesystem;

namespace {

constexpr int kMaxWalk = 8;
constexpr int kScanCap = 4000;  // a tree this big is not a new project; stop looking

struct Lang {
    std::string_view exts;
    std::string_view name;
};

constexpr std::array<Lang, 9> kLangs{{
    {"|.cpp|.hpp|.cc|.hh|.cxx|", "C++"},
    {"|.c|.h|", "C"},
    {"|.rs|", "Rust"},
    {"|.zig|", "Zig"},
    {"|.go|", "Go"},
    {"|.py|", "Python"},
    {"|.ts|.tsx|", "TypeScript"},
    {"|.js|.jsx|.vue|.svelte|", "JavaScript"},
    {"|.sh|", "shell"},
}};

// A build script does not establish what a project is written in — a fresh C++ project
// very often starts with build.sh and nothing else.
bool settles_language(std::string_view path) {
    return metal::is_source(path) && metal::ext_of(path) != ".sh";
}

std::string_view lang_of(std::string_view path) {
    const std::string ext = metal::ext_of(path);
    for (const auto& l : kLangs) {
        if (l.exts.find("|" + ext + "|") != std::string_view::npos) return l.name;
    }
    return {};
}

// Cheapest possible first question: does this very directory already hold source? In an
// established project the answer is yes on the first or second entry, and we stop there.
bool has_source(const fs::path& dir, bool recurse, std::string_view exclude) {
    std::error_code ec;
    int seen = 0;
    if (!recurse) {
        for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
            const std::string p = it->path().string();
            if (p != exclude && settles_language(p)) return true;
            if (++seen > kScanCap) return true;
        }
        return false;
    }
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    for (fs::recursive_directory_iterator end; it != end && !ec; it.increment(ec)) {
        const std::string p = it->path().string();
        if (metal::in_skipped_dir(p)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (p != exclude && settles_language(p)) return true;
        if (++seen > kScanCap) return true;
    }
    return false;
}

std::string frame(std::string_view dir, std::string_view lang) {
    return std::string(dir) +
           " holds no source yet, so this write sets the language to " + std::string(lang) +
           ".\nOne pass before it sets:\n"
           "  C++     total control, nothing between the code and the machine. Parsers,\n"
           "          protocols, state machines, hot paths, embedded, GPU - and anything that\n"
           "          must still behave identically in a year. Costs the most to write.\n"
           "  C       the same with fewer abstractions: freestanding, stable ABI, tiny\n"
           "          binaries, or existing C to interop with.\n"
           "  Rust    the compiler enforces memory safety; strongest where concurrency is the\n"
           "          risk and the team will not hold the invariant by hand.\n"
           "  Zig     C's simplicity with modern tooling. Production-real - TigerBeetle,\n"
           "          Ghostty, Bun - and reaching 1.0 in 2026.\n"
           "  Go      network services and CLIs, where deploy speed beats the last 20% of\n"
           "          control and a GC pause is not a correctness problem.\n"
           "  Python  a script, glue, or where the library IS the ecosystem: ML, data,\n"
           "          scraping, notebooks.\n"
           "  TS/JS   when it has to run in a browser. Anything else there is a choice, not a\n"
           "          constraint.\n"
           "\nTwo questions, in order:\n"
           "  1. Does it need control a runtime would take away - memory layout, timing, no GC\n"
           "     pause, a hardware or ABI boundary?  -> the lowest rung that fits, C++ first.\n"
           "  2. Is it a one-shot you run by hand, or under ~100 lines?  -> the highest rung\n"
           "     that fits. C++ here is excessive work for nothing, and metal is not asking\n"
           "     for it.\n"
           "\nName the choice and the reason in one line, then write. metal asks once per\n"
           "project; every later file in this tree is silent.";
}

}  // namespace

std::optional<std::string> choose_check(std::string_view doc) {
    if (hj::get(doc, "hook_event_name").value_or("") != "PreToolUse") return std::nullopt;
    const auto path = hj::get(doc, "tool_input", "file_path").value_or("");
    if (path.empty() || !metal::is_source(path)) return std::nullopt;
    if (metal::in_skipped_dir(path) || metal::is_throwaway(path)) return std::nullopt;
    const auto lang = lang_of(path);
    if (lang.empty()) return std::nullopt;

    std::error_code ec;
    const fs::path abs = fs::absolute(fs::path(path), ec);
    const fs::path dir = abs.parent_path();
    // Fast reject: a sibling source file means the language was settled before now.
    if (has_source(dir, false, abs.string())) return std::nullopt;

    // Walk up to what looks like the project root, then check the whole tree once.
    fs::path root = dir;
    for (int i = 0; i < kMaxWalk; ++i) {
        if (fs::exists(root / ".git", ec)) break;
        const fs::path up = root.parent_path();
        if (up.empty() || up == root || up == root.root_path()) break;
        root = up;
    }
    if (has_source(root, true, abs.string())) return std::nullopt;

    const std::string key = "choose:" + root.string();
    if (metal::already_said(key, "asked")) return std::nullopt;
    metal::remember(key, "asked");
    return metal::payload("PreToolUse", "permissionDecisionReason",
                          frame(root.string(), lang), "allow");
}
