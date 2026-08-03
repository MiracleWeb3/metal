// Count how many times the same file has been refused, so the third refusal can say
// something the first one did not.
//
// Measured across 414 real denials: the message never changes, and neither does the
// behaviour it is trying to correct. One file drew 96 denials across 6 sessions, 32 of them
// in a single session, every attempt between 5,531 and 5,698 lines. The limit was doing its
// job perfectly and the write was retried whole, unchanged, thirty-two times. Repeating
// identical advice to something that has ignored it thirty-one times is not enforcement, it
// is a loop.
//
// The streak expires after a day. "You have tried this 32 times" has to mean today, not a
// grudge carried from last month, or the escalated message becomes the wallpaper it exists
// to prevent.
#pragma once

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "stamp.hpp"

namespace metal {

inline constexpr long long kStreakWindow = 86400;  // one day

inline sfs::path deny_record_for(std::string_view path) {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    sfs::path base = xdg && *xdg ? sfs::path(xdg) : sfs::path(home ? home : "/tmp") / ".cache";
    return base / "metal" / "denied" / fnv1a(path);
}

// How many times this exact path was already denied, within the window. 0 on a first
// refusal, a stale record, or when the throttle is switched off for tests.
inline int deny_streak(std::string_view path) {
    if (std::getenv("METAL_NO_STAMP")) return 0;
    const std::string body = slurp(deny_record_for(path));
    if (body.empty()) return 0;
    int count = 0;
    long long when = 0;
    if (std::sscanf(body.c_str(), "%d %lld", &count, &when) != 2) return 0;
    if (static_cast<long long>(std::time(nullptr)) - when > kStreakWindow) return 0;
    return count < 0 ? 0 : count;
}

inline void record_deny(std::string_view path) {
    if (std::getenv("METAL_NO_STAMP")) return;
    const int next = deny_streak(path) + 1;
    std::error_code ec;
    const sfs::path p = deny_record_for(path);
    sfs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary)
        << next << " " << static_cast<long long>(std::time(nullptr));
}

}  // namespace metal
