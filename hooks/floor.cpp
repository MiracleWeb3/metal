// metal: rule 3 — the toolchain refuses.
//
// Rule 1 governs a property of the file. This governs a property of the build, because
// every ecosystem ships a strict mode and almost nobody turns it on. SKILL.md used to
// concede the point outright: "discipline comes from style, not from the compiler
// refusing." That sentence gave away the exact fight metal exists to win.
//
// Measured on one real machine: 433 build invocations, 431 uses of -Werror, 47 of
// -fsanitize. The discipline is genuine — and it lives in hand-written per-project build
// files, so it drifts the moment attention moves. It already had: fifteen of fifteen
// modules in one project carried the floor, while a second had -Wall -Wextra and no
// -Werror, and a third had no build file at all.
//
// C++ is the deepest instance, not the only one. `strict: true` is the same shape as
// -Werror — one switch that turns a class of runtime bugs into build failures — and a
// vibe-coded tsconfig with strict off is the identical disease one ecosystem over.
//
// Advisory, never a deny. The offending artifact is a config file up the tree, not the
// source being written.
#include <array>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common.hpp"
#include "stamp.hpp"

namespace fs = std::filesystem;

namespace {

constexpr int kMaxWalk = 8;

// How a floor is recognised in its config file.
enum class Kind {
    Flags,     // every needle must appear literally (compiler flags)
    JsonTrue,  // "needle": true, whitespace-tolerant
    AnyOf,     // at least one needle appears (a checker is configured at all)
};

struct Rule {
    std::string_view exts;     // pipe-delimited, e.g. "|.c|.cpp|"
    std::string_view configs;  // pipe-delimited, in search order
    Kind kind;
    std::string_view needles;  // pipe-delimited
    std::string_view floor;    // what the floor is, in one line
};

// Ordered by depth, which is also metal's order of preference. Rust and Go are absent on
// purpose: rule 2 does not send you there, so rule 3 has nothing to enforce for them.
constexpr std::array<Rule, 3> kRules{{
    {"|.c|.h|.cpp|.hpp|.cc|.hh|.cxx|", "|build.sh|CMakeLists.txt|Makefile|makefile|meson.build|",
     Kind::Flags, "|-Wall|-Wextra|-Werror|",
     "-std=c++20 -Wall -Wextra -Werror, plus -fsanitize=address,undefined "
     "-D_GLIBCXX_ASSERTIONS for a debug build"},
    {"|.ts|.tsx|", "|tsconfig.json|", Kind::JsonTrue, "|strict|",
     "\"strict\": true in tsconfig.json - one switch that turns on eight checks, the same "
     "shape as -Werror"},
    {"|.py|", "|pyproject.toml|mypy.ini|.mypy.ini|pyrightconfig.json|setup.cfg|", Kind::AnyOf,
     "|[tool.mypy]|[tool.pyright]|[tool.pyrefly]|[tool.ty]|[mypy]|typeCheckingMode|",
     "a type checker configured at all - mypy, pyright, pyrefly or ty; metal does not care "
     "which, only that something checks"},
}};

std::vector<std::string_view> split(std::string_view s) {
    std::vector<std::string_view> out;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '|') { ++i; continue; }
        const auto end = s.find('|', i);
        out.push_back(s.substr(i, end == std::string_view::npos ? end : end - i));
        if (end == std::string_view::npos) break;
        i = end;
    }
    return out;
}

// "strict"  :  true — tolerant of spacing, because config files are hand-written.
bool json_true(std::string_view text, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    for (auto p = text.find(needle); p != std::string_view::npos; p = text.find(needle, p + 1)) {
        std::size_t i = p + needle.size();
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i >= text.size() || text[i] != ':') continue;
        ++i;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (text.compare(i, 4, "true") == 0) return true;
    }
    return false;
}

const Rule* rule_for(std::string_view path) {
    const std::string ext = metal::ext_of(path);
    if (ext.empty()) return nullptr;
    for (const auto& r : kRules) {
        for (const auto e : split(r.exts)) {
            if (e == ext) return &r;
        }
    }
    return nullptr;
}

}  // namespace

std::optional<std::string> floor_check(std::string_view doc) {
    const auto path = hj::get(doc, "tool_input", "file_path").value_or("");
    if (path.empty() || metal::in_skipped_dir(path) || metal::is_throwaway(path)) {
        return std::nullopt;
    }
    const Rule* rule = rule_for(path);
    if (!rule) return std::nullopt;

    std::error_code ec;
    fs::path dir = fs::absolute(fs::path(path), ec).parent_path();
    fs::path found, stopped;
    const auto configs = split(rule->configs);
    for (int i = 0; i < kMaxWalk && !dir.empty(); ++i) {
        for (const auto name : configs) {
            const fs::path cand = dir / std::string(name);
            if (fs::exists(cand, ec)) { found = cand; break; }
        }
        stopped = dir;
        if (!found.empty()) break;
        if (fs::exists(dir / ".git", ec)) break;  // repo root: stop, do not escape the project
        const fs::path up = dir.parent_path();
        if (up.empty() || up == dir) break;
        dir = up;
    }

    if (found.empty()) {
        const std::string key = stopped.string();
        if (metal::already_said(key, "absent")) return std::nullopt;
        metal::remember(key, "absent");
        std::string looked;
        for (const auto c : configs) looked += (looked.empty() ? "" : ", ") + std::string(c);
        return metal::payload("PostToolUse", "additionalContext",
                              "Nothing above " + stopped.string() +
                                  " is checking this code (looked for " + looked +
                                  ").\nmetal's floor here: " + std::string(rule->floor) + ".");
    }

    const std::string text = metal::slurp(found);
    const auto needles = split(rule->needles);
    std::vector<std::string> missing;
    bool satisfied = false;
    switch (rule->kind) {
        case Kind::Flags:
            for (const auto n : needles) {
                if (text.find(n) == std::string::npos) missing.emplace_back(n);
            }
            satisfied = missing.empty();
            break;
        case Kind::JsonTrue:
            satisfied = json_true(text, needles.front());
            break;
        case Kind::AnyOf:
            for (const auto n : needles) {
                if (text.find(n) != std::string::npos) { satisfied = true; break; }
            }
            break;
    }
    // A missing sanitizer is never a violation on its own. Tried it the other way and it
    // fired on 11 of the 15 modules in the one project that meets the floor everywhere; a
    // check that scolds your best code is one you learn to ignore.
    if (satisfied) return std::nullopt;

    const std::string key = found.string();
    const std::string token = metal::mtime_of(found);
    if (metal::already_said(key, token)) return std::nullopt;
    metal::remember(key, token);

    std::string body = found.string() + " governs this file";
    if (!missing.empty()) {
        body += " and is missing ";
        for (std::size_t i = 0; i < missing.size(); ++i) {
            body += (i && i + 1 == missing.size()) ? " and " : (i ? ", " : "");
            body += missing[i];
        }
    } else {
        body += " but does not meet the floor";
    }
    body += ".\nmetal's floor here: " + std::string(rule->floor) +
            ".\nA warning you are allowed to ignore is a bug that compiles.";
    if (rule->kind == Kind::Flags && text.find("fsanitize") == std::string::npos) {
        body += "\nThere is no sanitizer mode either.";
    }
    return metal::payload("PostToolUse", "additionalContext", body);
}
