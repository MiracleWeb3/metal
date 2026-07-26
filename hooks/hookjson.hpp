// Exactly enough JSON to read one hook event, and nothing more.
//
// No library, because metal's own rule is that a dependency you have not read is a
// runtime you cannot predict — and this runs on every Write you make.
//
// It has to be a real scanner rather than a substring search: the event carries the
// file's entire content as a string, so a naive search for "file_path" finds a hit
// inside any source file that happens to contain that text. Strings are skipped as
// strings, which is the whole reason this exists.
#pragma once

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace hj {

inline void skip_ws(std::string_view s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

inline void utf8_append(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Decodes the string at s[i] == '"', leaving i past the closing quote. The escapes
// matter: line counting is done on the decoded text, so a \n that stayed literal
// would make a 400-line file look like one line.
inline bool read_string(std::string_view s, std::size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        const char c = s[i++];
        if (c == '"') return true;
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (i >= s.size()) return false;
        switch (const char e = s[i++]) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case '"':
            case '\\':
            case '/': out.push_back(e); break;
            case 'u': {
                if (i + 4 > s.size()) return false;
                unsigned cp = 0;
                for (int k = 0; k < 4; ++k) {
                    const char h = s[i + k];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                    else return false;
                }
                i += 4;
                utf8_append(out, cp);  // a lone surrogate becomes its own glyph; we only count lines
                break;
            }
            default: return false;
        }
    }
    return false;
}

// Steps over any value: object, array, string, number, literal.
inline bool skip_value(std::string_view s, std::size_t& i) {
    skip_ws(s, i);
    if (i >= s.size()) return false;
    if (s[i] == '"') {
        std::string ignored;
        return read_string(s, i, ignored);
    }
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '"') {
                std::string ignored;
                if (!read_string(s, i, ignored)) return false;
                continue;
            }
            if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                if (--depth == 0) {
                    ++i;
                    return true;
                }
            }
            ++i;
        }
        return false;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;
    return true;
}

inline bool enter_object(std::string_view s, std::size_t& i) {
    skip_ws(s, i);
    if (i >= s.size() || s[i] != '{') return false;
    ++i;
    return true;
}

// With i just past '{', leaves i at the value for `key`. False if the object ends first.
inline bool find_key(std::string_view s, std::size_t& i, std::string_view key) {
    for (;;) {
        skip_ws(s, i);
        if (i >= s.size() || s[i] == '}') return false;
        if (s[i] == ',') {
            ++i;
            continue;
        }
        std::string k;
        if (!read_string(s, i, k)) return false;
        skip_ws(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        if (k == key) {
            skip_ws(s, i);
            return true;
        }
        if (!skip_value(s, i)) return false;
    }
}

// get(doc, "tool_input", "file_path") — nullopt for absent, or anything not a string.
inline std::optional<std::string> get(std::string_view doc, std::string_view a,
                                      std::string_view b = {}) {
    std::size_t i = 0;
    if (!enter_object(doc, i) || !find_key(doc, i, a)) return std::nullopt;
    if (!b.empty()) {
        if (!enter_object(doc, i) || !find_key(doc, i, b)) return std::nullopt;
    }
    std::string v;
    if (!read_string(doc, i, v)) return std::nullopt;
    return v;
}

inline std::string esc(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (const char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned char>(c));
                    o += buf;
                } else {
                    o.push_back(c);
                }
        }
    }
    return o;
}

}  // namespace hj
