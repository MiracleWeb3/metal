// metal: refuse oversized source files.
//
// Wired to PreToolUse(Write) — denies the write outright — and PostToolUse(Write|Edit)
// — warns when a file is over the limit, or approaching it. Self-check: ./split --selftest
//
// Two thresholds, because one only ever spoke at failure. Measured across 109 authored
// C/C++ files: median 78, p90 202, max 299, nothing over. That last number is the tell —
// files were being squeezed under the ceiling (299, 296, 289) rather than split, because
// nothing said anything until the deny landed at 301, which is the worst moment to go
// looking for a seam. WARN fires at 220 on 8% of those files, while there is still slack.
//
// The override exists because "never exceed 300" and "never cut at an arbitrary line"
// contradict each other for a genuinely cohesive unit — a lexer, a transition table, a
// generated parser. Without a way to say yes, the only legal move on such a file is a bad
// split. The marker makes the exception cost something: a number and a written reason,
// sitting in the file, visible in review.
//
// C++ and not Python because this is metal's own hook, and a skill that mandates C++
// should not be enforced by an interpreter. It also runs on every Write, where process
// startup is the whole cost.
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "hookjson.hpp"

namespace {

constexpr int kLimit = 300;         // keep in sync with SKILL.md
constexpr int kWarn = 220;          // advisory only; ~8% of real files, all with runway left
constexpr int kMaxOverride = 600;   // bounded: an exception that can name any size is a repeal
constexpr std::size_t kMinReason = 20;  // "because" is not a reason

constexpr std::array<std::string_view, 29> kCode{
    ".rs", ".c", ".h", ".cpp", ".hpp", ".cc", ".zig", ".go", ".py", ".js", ".jsx", ".ts",
    ".tsx", ".vue", ".svelte", ".java", ".rb", ".php", ".sh", ".swift", ".kt", ".cs",
    ".lua", ".ex", ".exs", ".sql", ".asm", ".s", ".mm"};

constexpr std::array<std::string_view, 6> kSkip{"node_modules", "target", "vendor",
                                                "dist", "build", ".git"};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string basename_of(std::string_view p) {
    const auto slash = p.find_last_of('/');
    return std::string(slash == std::string_view::npos ? p : p.substr(slash + 1));
}

bool is_source(std::string_view path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos) return false;
    const std::string ext = lower(std::string(path.substr(dot)));
    return std::find(kCode.begin(), kCode.end(), ext) != kCode.end();
}

bool in_skipped_dir(std::string_view path) {
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

int line_count(std::string_view s) {
    int n = 0;
    for (const char c : s) {
        if (c == '\n') ++n;
    }
    if (!s.empty() && s.back() != '\n') ++n;
    return n;
}

// A hand scanner, not std::regex: metal's own rule, and the grammar is four tokens.
//   // metal: allow 380 - one lexer state machine, the transition table does not split
struct Grant {
    int allowance;
    std::string reason;
};

bool comment_before(std::string_view s, std::size_t p) {
    while (p > 0 && (s[p - 1] == ' ' || s[p - 1] == '\t')) --p;
    if (p >= 2 && (s.substr(p - 2, 2) == "//" || s.substr(p - 2, 2) == "/*" ||
                   s.substr(p - 2, 2) == "--")) {
        return true;
    }
    return p >= 1 && s[p - 1] == '#';
}

bool eat(std::string_view s, std::size_t& i, std::string_view word) {
    if (s.compare(i, word.size(), word) != 0) return false;
    i += word.size();
    return true;
}

void eat_blanks(std::string_view s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
}

std::optional<Grant> override_of(std::string_view text) {
    for (std::size_t p = text.find("metal:"); p != std::string_view::npos;
         p = text.find("metal:", p + 1)) {
        if (!comment_before(text, p)) continue;
        std::size_t i = p + 6;
        eat_blanks(text, i);
        if (!eat(text, i, "allow")) continue;
        eat_blanks(text, i);
        int allowance = 0;
        const std::size_t digits = i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            allowance = allowance * 10 + (text[i++] - '0');
            if (allowance > 1'000'000) break;  // absurd input stops being a number
        }
        if (i == digits) continue;
        eat_blanks(text, i);
        // separator: '-', ':' or an em dash (3 bytes), at least one
        bool sep = false;
        for (;;) {
            if (i < text.size() && (text[i] == '-' || text[i] == ':')) { ++i; sep = true; }
            else if (text.compare(i, 3, "\xe2\x80\x94") == 0) { i += 3; sep = true; }
            else break;
        }
        if (!sep) continue;
        eat_blanks(text, i);
        const auto eol = text.find('\n', i);
        std::string reason(text.substr(i, eol == std::string_view::npos ? eol : eol - i));
        while (!reason.empty() && (reason.back() == ' ' || reason.back() == '\r')) reason.pop_back();
        if (reason.size() >= 2 && reason.compare(reason.size() - 2, 2, "*/") == 0) {
            reason.erase(reason.size() - 2);
        }
        while (!reason.empty() && reason.back() == ' ') reason.pop_back();
        if (allowance > kMaxOverride || reason.size() < kMinReason) continue;
        return Grant{allowance, reason};
    }
    return std::nullopt;
}

std::string advice(std::string_view path, int n) {
    const int suggest = std::min(n + 40, kMaxOverride);
    return basename_of(path) + " is " + std::to_string(n) + " lines; the limit is " +
           std::to_string(kLimit) +
           ". Split it now, before anything else.\nMake a directory named after the file, give "
           "each concern its own file, re-export from one entry point (C++ header, C header, TS "
           "index). Cut along seams that already exist - parse/emit/state/io - not at an arbitrary "
           "line.\nIf this file genuinely does not split, say so in it and why:\n  // metal: allow " +
           std::to_string(suggest) + " - <what makes it one unit>";
}

std::string nudge(std::string_view path, int n) {
    return basename_of(path) + " is " + std::to_string(n) + " lines, " +
           std::to_string(kLimit - n) +
           " from the limit. Find the seam now while there is still slack - parse/emit/state/io. "
           "Splitting at 301 means cutting wherever you happen to be, which is how one coherent "
           "file becomes two incoherent ones.";
}

std::string payload(std::string_view event, std::string_view field, std::string_view body,
                    std::string_view decision = {}) {
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

}  // namespace

// Returns the hook's stdout payload, or nullopt to stay quiet.
std::optional<std::string> check(std::string_view doc) {
    const auto path = hj::get(doc, "tool_input", "file_path").value_or("");
    if (path.empty() || !is_source(path) || in_skipped_dir(path)) return std::nullopt;

    const bool pre = hj::get(doc, "hook_event_name").value_or("") == "PreToolUse";
    std::string text;
    if (pre) {
        text = hj::get(doc, "tool_input", "content").value_or("");
    } else {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        std::ostringstream buf;
        buf << in.rdbuf();
        text = buf.str();
    }
    const int n = line_count(text);

    if (const auto grant = override_of(text); grant && n <= grant->allowance) return std::nullopt;
    if (n <= kWarn) return std::nullopt;

    if (n > kLimit) {
        // Creating an oversized file is denied outright; editing one that is already
        // oversized only warns, so a one-line fix to legacy code is not held hostage.
        return pre ? payload("PreToolUse", "permissionDecisionReason", advice(path, n), "deny")
                   : payload("PostToolUse", "additionalContext", advice(path, n));
    }
    // In the warn band. Advisory on both paths: a fresh 250-line file is the cheapest
    // possible moment to hear it, and PreToolUse can allow while still saying something.
    return pre ? payload("PreToolUse", "permissionDecisionReason", nudge(path, n), "allow")
               : payload("PostToolUse", "additionalContext", nudge(path, n));
}

#ifdef METAL_SELFTEST
#include "selftest.inc"
#endif

int main(int argc, char** argv) {
#ifdef METAL_SELFTEST
    if (argc > 1 && std::string_view(argv[1]) == "--selftest") return selftest();
#else
    (void)argc;
    (void)argv;
#endif
    std::ostringstream buf;
    buf << std::cin.rdbuf();
    // Fail open, always: this gates every Write, and a hook that dies must not take the
    // edit with it.
    if (const auto out = check(buf.str())) std::cout << *out << "\n";
    return 0;
}
