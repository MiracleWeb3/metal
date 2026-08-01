// Shared between metal's two enforced rules: rule 1 (no large files, split.cpp) and
// rule 3 (the compiler refuses, floor.cpp).
//
// Header-only and inline so the hook stays a two-translation-unit build with no build
// system — metal's own rule about dependencies applies to its own machinery first.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "hookjson.hpp"

namespace metal {

// Everything rule 1 counts lines in. Deliberately wider than the languages rule 2 permits:
// an existing codebase in another language still benefits from files that fit on a screen.
inline constexpr std::array<std::string_view, 29> kCode{
    ".rs", ".c", ".h", ".cpp", ".hpp", ".cc", ".zig", ".go", ".py", ".js", ".jsx", ".ts",
    ".tsx", ".vue", ".svelte", ".java", ".rb", ".php", ".sh", ".swift", ".kt", ".cs",
    ".lua", ".ex", ".exs", ".sql", ".asm", ".s", ".mm"};

// What rule 3 governs: only C and C++ have the compiler this rule is about.
inline constexpr std::array<std::string_view, 7> kCxx{".c",   ".h",  ".cpp", ".hpp",
                                                      ".cc",  ".hh", ".cxx"};

inline constexpr std::array<std::string_view, 6> kSkip{"node_modules", "target", "vendor",
                                                       "dist", "build", ".git"};

inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline std::string basename_of(std::string_view p) {
    const auto slash = p.find_last_of('/');
    return std::string(slash == std::string_view::npos ? p : p.substr(slash + 1));
}

inline std::string ext_of(std::string_view path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos) return {};
    return lower(std::string(path.substr(dot)));
}

inline bool is_source(std::string_view path) {
    const std::string ext = ext_of(path);
    return !ext.empty() && std::find(kCode.begin(), kCode.end(), ext) != kCode.end();
}

inline bool is_cxx(std::string_view path) {
    const std::string ext = ext_of(path);
    return !ext.empty() && std::find(kCxx.begin(), kCxx.end(), ext) != kCxx.end();
}

inline bool in_skipped_dir(std::string_view path) {
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const auto seg = path.substr(start, end == std::string_view::npos ? end : end - start);
        if (std::find(kSkip.begin(), kSkip.end(), seg) != kSkip.end()) return true;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return false;
}

// SKILL.md's escape (a) — one-shot glue that never enters a repo — made mechanical.
// Measured: of 207 candidate firings for an earlier version of rule 3, 139 were scratchpad
// noise. A check that is wrong two times in three is one you learn to route around.
inline bool is_throwaway(std::string_view path) {
    return path.find("/scratchpad/") != std::string_view::npos ||
           path.rfind("/tmp/", 0) == 0;
}

inline std::string payload(std::string_view event, std::string_view field,
                           std::string_view body, std::string_view decision = {}) {
    std::string out = "{\"hookSpecificOutput\":{\"hookEventName\":\"";
    out += event;
    out += "\",";
    if (!decision.empty()) {
        out += "\"permissionDecision\":\"";
        out += decision;
        out += "\",";
    }
    out += "\"";
    out += field;
    out += "\":\"";
    out += hj::esc(body);
    out += "\"}}";
    return out;
}

}  // namespace metal

// Rule 1 — no large files. Defined in split.cpp.
std::optional<std::string> check(std::string_view doc);

// Rule 3 — the compiler refuses. Defined in floor.cpp.
std::optional<std::string> floor_check(std::string_view doc);
