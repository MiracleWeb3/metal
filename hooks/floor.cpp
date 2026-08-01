// metal: rule 3 — the compiler refuses.
//
// Rule 1 governs a property of the file. This governs a property of the build, because in
// C++ the compiler is an enforcement instrument and nothing was using it. SKILL.md used to
// concede the point outright: "discipline comes from style, not from the compiler
// refusing." That sentence gave away the exact fight metal exists to win.
//
// Measured on one real machine: 433 build invocations, 431 uses of -Werror, 47 of
// -fsanitize. The discipline is genuine — and it lives in hand-written per-project build
// files, so it drifts the moment attention moves. It already had: fifteen of fifteen
// modules in one project carried the floor, while a second project had -Wall -Wextra and
// no -Werror, and a third had no build file at all.
//
// Advisory, never a deny. Denying a source write because a build file is wrong punishes
// the wrong artifact.
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "common.hpp"

namespace fs = std::filesystem;

namespace {

constexpr std::array<std::string_view, 5> kBuildFiles{"build.sh", "CMakeLists.txt", "Makefile",
                                                      "makefile", "meson.build"};
constexpr std::array<std::string_view, 3> kFloor{"-Wall", "-Wextra", "-Werror"};
constexpr int kMaxWalk = 8;

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

std::string fnv1a(std::string_view s) {
    unsigned long long h = 1469598103934665603ULL;
    for (const char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ULL;
    }
    char out[24];
    std::snprintf(out, sizeof out, "%016llx", h);
    return out;
}

// One warning per build file, re-armed when that build file changes — otherwise writing
// twelve files into a project emits twelve identical lines and the check becomes wallpaper.
fs::path stamp_for(std::string_view key) {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    fs::path base = xdg && *xdg ? fs::path(xdg) : fs::path(home ? home : "/tmp") / ".cache";
    return base / "metal" / "seen" / fnv1a(key);
}

bool already_said(std::string_view key, const std::string& token) {
    if (std::getenv("METAL_NO_STAMP")) return false;
    std::error_code ec;
    const fs::path s = stamp_for(key);
    if (!fs::exists(s, ec)) return false;
    return read_file(s) == token;
}

void remember(std::string_view key, const std::string& token) {
    if (std::getenv("METAL_NO_STAMP")) return;
    std::error_code ec;
    const fs::path s = stamp_for(key);
    fs::create_directories(s.parent_path(), ec);
    std::ofstream(s, std::ios::binary) << token;
}

std::string mtime_of(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return "0";
    // The cast is not decoration: file_time_type::rep is long on libstdc++ and a wider type
    // on libc++, where to_string is then ambiguous. This is only a change-detector, so a
    // narrowing conversion costs nothing.
    return std::to_string(static_cast<long long>(t.time_since_epoch().count()));
}

}  // namespace

std::optional<std::string> floor_check(std::string_view doc) {
    const auto path = hj::get(doc, "tool_input", "file_path").value_or("");
    if (path.empty() || !metal::is_cxx(path)) return std::nullopt;
    if (metal::in_skipped_dir(path) || metal::is_throwaway(path)) return std::nullopt;

    std::error_code ec;
    fs::path dir = fs::absolute(fs::path(path), ec).parent_path();
    fs::path found, stopped;
    for (int i = 0; i < kMaxWalk && !dir.empty(); ++i) {
        for (const auto name : kBuildFiles) {
            const fs::path cand = dir / std::string(name);
            if (fs::exists(cand, ec)) {
                found = cand;
                break;
            }
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
        if (already_said(key, "absent")) return std::nullopt;
        remember(key, "absent");
        return metal::payload(
            "PostToolUse", "additionalContext",
            "No build file above " + stopped.string() +
                " (looked for build.sh, CMakeLists.txt, Makefile, meson.build).\nC++ without a "
                "build that carries -Wall -Wextra -Werror is C++ with its safety systems switched "
                "off - which is most of what the language was chosen for.\nmetal's floor: "
                "-std=c++20 -Wall -Wextra -Werror, plus -fsanitize=address,undefined "
                "-D_GLIBCXX_ASSERTIONS for a debug build.");
    }

    const std::string text = read_file(found);
    std::vector<std::string> missing;
    for (const auto flag : kFloor) {
        if (text.find(flag) == std::string::npos) missing.emplace_back(flag);
    }
    // A missing sanitizer is never a violation on its own. Tried the other way first and it
    // fired on 11 of the 15 modules in the one project that meets the floor everywhere — a
    // check that scolds the best codebase on the machine is one you learn to ignore.
    // It rides along only when a real flag is already missing.
    if (missing.empty()) return std::nullopt;
    const bool no_san = text.find("fsanitize") == std::string::npos;

    const std::string key = found.string();
    const std::string token = mtime_of(found);
    if (already_said(key, token)) return std::nullopt;
    remember(key, token);

    std::string body = found.string() + " governs this file and is missing ";
    for (std::size_t i = 0; i < missing.size(); ++i) {
        body += (i && i + 1 == missing.size()) ? " and " : (i ? ", " : "");
        body += missing[i];
    }
    body +=
        ".\nmetal's floor is -std=c++20 -Wall -Wextra -Werror. A warning you are allowed to "
        "ignore is a bug that compiles.";
    if (no_san) {
        body +=
            "\nThere is no sanitizer mode either; a debug build wants "
            "-fsanitize=address,undefined -D_GLIBCXX_ASSERTIONS.";
    }
    return metal::payload("PostToolUse", "additionalContext", body);
}
