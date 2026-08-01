// Say a thing once, and again only when the thing being judged changes.
//
// Without this, writing twelve files into one project emits twelve identical warnings and
// the check becomes wallpaper — the failure mode that kills advisory tooling. The token is
// the judged file's mtime, so the message re-arms exactly when someone edits it.
//
// METAL_NO_STAMP=1 disables the throttle: used by the selftest, and available to anyone who
// would rather hear it every time.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace metal {

namespace sfs = std::filesystem;

inline std::string slurp(const sfs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

inline std::string fnv1a(std::string_view s) {
    unsigned long long h = 1469598103934665603ULL;
    for (const char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ULL;
    }
    char out[24];
    std::snprintf(out, sizeof out, "%016llx", h);
    return out;
}

inline sfs::path stamp_for(std::string_view key) {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    sfs::path base = xdg && *xdg ? sfs::path(xdg) : sfs::path(home ? home : "/tmp") / ".cache";
    return base / "metal" / "seen" / fnv1a(key);
}

inline bool already_said(std::string_view key, const std::string& token) {
    if (std::getenv("METAL_NO_STAMP")) return false;
    std::error_code ec;
    const sfs::path s = stamp_for(key);
    return sfs::exists(s, ec) && slurp(s) == token;
}

inline void remember(std::string_view key, const std::string& token) {
    if (std::getenv("METAL_NO_STAMP")) return;
    std::error_code ec;
    const sfs::path s = stamp_for(key);
    sfs::create_directories(s.parent_path(), ec);
    std::ofstream(s, std::ios::binary) << token;
}

inline std::string mtime_of(const sfs::path& p) {
    std::error_code ec;
    const auto t = sfs::last_write_time(p, ec);
    if (ec) return "0";
    // The cast is not decoration: file_time_type::rep is long on libstdc++ and wider on
    // libc++, where to_string is then ambiguous. This is only a change-detector.
    return std::to_string(static_cast<long long>(t.time_since_epoch().count()));
}

}  // namespace metal
