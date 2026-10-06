#include <libwxui.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utf8.h>

namespace wxui {

std::string NormalizeXmlIdentifier(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (char ch : sv) {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            out.push_back(static_cast<char>(std::tolower(uch)));
        }
    }
    return out;
}

std::string WxStringToUtf8(const wxString& value) {
    if (value.empty()) return {};
    const wxScopedCharBuffer utf8 = value.ToUTF8();
    if (!utf8.data()) return {};
    return std::string(utf8.data(), std::strlen(utf8.data()));
}

wxString Utf8ToWxString(std::string_view value) {
    if (value.empty()) return {};
    return wxString::FromUTF8(value.data(), value.size());
}

std::string Utf16ToUtf8(std::u16string_view value) {
    if (value.empty()) return {};
    std::string out;
    try {
        out.reserve(value.size() * 2);
        utf8::utf16to8(value.begin(), value.end(), std::back_inserter(out));
    } catch (...) {
        return {};
    }
    return out;
}

std::u16string Utf8ToUtf16(std::string_view value) {
    if (value.empty()) return {};
    std::u16string out;
    try {
        out.reserve(value.size());
        utf8::utf8to16(value.begin(), value.end(), std::back_inserter(out));
    } catch (...) {
        return {};
    }
    return out;
}

std::string Utf32ToUtf8(std::u32string_view value) {
    if (value.empty()) return {};
    std::string out;
    try {
        out.reserve(value.size() * 4);
        utf8::utf32to8(value.begin(), value.end(), std::back_inserter(out));
    } catch (...) {
        return {};
    }
    return out;
}

std::u32string Utf8ToUtf32(std::string_view value) {
    if (value.empty()) return {};
    std::u32string out;
    try {
        out.reserve(value.size());
        utf8::utf8to32(value.begin(), value.end(), std::back_inserter(out));
    } catch (...) {
        return {};
    }
    return out;
}

// ── ParseDWORD — "0xAARRGGBB" / "#RRGGBB" → wxColour ────────────────────
wxColour ParseDWORD(std::string_view sv, const wxColour& fallback) {
    while (!sv.empty() && sv.front() == ' ') sv.remove_prefix(1);
    if (sv.empty()) return fallback;

    uint32_t val = 0;
    bool hasAlpha = false;

    auto hexdigit = [](char c, uint8_t& out) -> bool {
        if (c >= '0' && c <= '9') { out = uint8_t(c - '0');       return true; }
        if (c >= 'a' && c <= 'f') { out = uint8_t(c - 'a' + 10);  return true; }
        if (c >= 'A' && c <= 'F') { out = uint8_t(c - 'A' + 10);  return true; }
        return false;
    };

    if (sv.starts_with("0x") || sv.starts_with("0X")) {
        sv.remove_prefix(2);
        if (sv.empty() || sv.size() > 8) return fallback;
        hasAlpha = (sv.size() == 8);
        for (char c : sv) {
            uint8_t d = 0;
            if (!hexdigit(c, d)) return fallback;
            val = (val << 4) | d;
        }
    } else if (sv.starts_with('#')) {
        sv.remove_prefix(1);
        if (sv.size() != 6 && sv.size() != 8) return fallback;
        hasAlpha = (sv.size() == 8);
        for (char c : sv) {
            uint8_t d = 0;
            if (!hexdigit(c, d)) return fallback;
            val = (val << 4) | d;
        }
        if (!hasAlpha) val |= 0xFF000000u;  // '#RRGGBB' → fully opaque
    } else {
        return fallback;
    }

    // When only 6 hex digits given after "0x" (no alpha), treat as opaque
    if (!hasAlpha && sv.size() <= 6) val |= 0xFF000000u;

    const uint8_t a = uint8_t((val >> 24) & 0xFF);
    const uint8_t r = uint8_t((val >> 16) & 0xFF);
    const uint8_t g = uint8_t((val >>  8) & 0xFF);
    const uint8_t b = uint8_t((val >>  0) & 0xFF);
    return wxColour(r, g, b, a);
}

// ── ParseRECT — preserve raw four-value tuples ───────────────────────────
wxRect ParseRECT(std::string_view sv, const wxRect& fallback) {
    int vals[4] = {fallback.x, fallback.y, fallback.width, fallback.height};
    int idx = 0;

    while (!sv.empty() && idx < 4) {
        while (!sv.empty() && (sv.front() == ' ')) sv.remove_prefix(1);
        if (sv.empty()) break;

        bool neg = false;
        if (sv.front() == '-') { neg = true; sv.remove_prefix(1); }
        if (sv.empty() || !std::isdigit(static_cast<unsigned char>(sv.front())))
            return fallback;

        int v = 0;
        while (!sv.empty() && std::isdigit(static_cast<unsigned char>(sv.front()))) {
            v = v * 10 + (sv.front() - '0');
            sv.remove_prefix(1);
        }
        vals[idx++] = neg ? -v : v;

        while (!sv.empty() && (sv.front() == ',' || sv.front() == ' '))
            sv.remove_prefix(1);
    }
    if (idx < 4) return fallback;
    return wxRect(vals[0], vals[1], vals[2], vals[3]);
}

// ── ParseSIZE — "width,height" → wxSize ──────────────────────────────────
wxSize ParseSIZE(std::string_view sv, const wxSize& fallback) {
    int vals[2] = {fallback.x, fallback.y};
    int idx = 0;

    while (!sv.empty() && idx < 2) {
        while (!sv.empty() && sv.front() == ' ') sv.remove_prefix(1);
        if (sv.empty()) break;

        bool neg = false;
        if (sv.front() == '-') { neg = true; sv.remove_prefix(1); }
        if (sv.empty() || !std::isdigit(static_cast<unsigned char>(sv.front())))
            return fallback;

        int v = 0;
        while (!sv.empty() && std::isdigit(static_cast<unsigned char>(sv.front()))) {
            v = v * 10 + (sv.front() - '0');
            sv.remove_prefix(1);
        }
        vals[idx++] = neg ? -v : v;

        while (!sv.empty() && (sv.front() == ',' || sv.front() == ' '))
            sv.remove_prefix(1);
    }

    return idx == 2 ? wxSize(vals[0], vals[1]) : fallback;
}

// ── ParseBOOL ────────────────────────────────────────────────────────────
bool ParseBOOL(std::string_view sv, bool fallback) {
    if (sv == "true"  || sv == "True"  || sv == "TRUE"  || sv == "1") return true;
    if (sv == "false" || sv == "False" || sv == "FALSE" || sv == "0") return false;
    return fallback;
}

// ── ParseINT ─────────────────────────────────────────────────────────────
int ParseINT(std::string_view sv, int fallback) {
    while (!sv.empty() && sv.front() == ' ') sv.remove_prefix(1);
    if (sv.empty()) return fallback;
    int result = fallback;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), result);
    return (ec == std::errc{}) ? result : fallback;
}

// ── ParseImageSpec ────────────────────────────────────────────────────────
//
// Handles both:
//   "path.png"
//   "file='path.png' res='' restype='0' dest='0,0,0,0' source='0,0,0,0'
//    corner='4,4,4,4' mask='#FF0000' fade='255' hole='false'
//    xtiled='false' ytiled='false'"
ImageSpec ParseImageSpec(std::string_view sv) {
    ImageSpec spec;
    while (!sv.empty() && sv.front() == ' ') sv.remove_prefix(1);
    if (sv.empty()) return spec;

    // Short form: no '=' characters in value
    if (sv.find('=') == std::string_view::npos) {
        spec.path = std::string{sv};
        return spec;
    }

    // Full key=value form; values may be single-quoted
    auto extractVal = [](std::string_view s) -> std::string {
        if (!s.empty() && s.front() == '\'') {
            s.remove_prefix(1);
            auto end = s.find('\'');
            if (end != std::string_view::npos)
                return std::string{s.substr(0, end)};
            return std::string{s};
        }
        auto end = s.find(' ');
        return std::string{s.substr(0, end)};
    };

    while (!sv.empty()) {
        while (!sv.empty() && sv.front() == ' ') sv.remove_prefix(1);
        if (sv.empty()) break;

        auto eq = sv.find('=');
        if (eq == std::string_view::npos) break;

        std::string key{sv.substr(0, eq)};
        sv.remove_prefix(eq + 1);

        // skip quote
        std::size_t skip = 0;
        if (!sv.empty() && sv.front() == '\'') {
            sv.remove_prefix(1);
            auto end = sv.find('\'');
            std::string val{sv.substr(0, end)};
            sv.remove_prefix(end != std::string_view::npos ? end + 1 : sv.size());

            if (key == "file")    spec.path      = val;
            else if (key == "res")      spec.resName   = val;
            else if (key == "restype")  spec.resType   = ParseINT(val, 0);
            else if (key == "dest")     spec.dest      = ParseRECT(val);
            else if (key == "source")   spec.source    = ParseRECT(val);
            else if (key == "corner")   spec.corner    = ParseRECT(val);
            else if (key == "mask")     spec.maskColor = ParseDWORD(val);
            else if (key == "fade")     spec.fade      = uint8_t(ParseINT(val, 255));
            else if (key == "hole")     spec.hole      = ParseBOOL(val);
            else if (key == "xtiled")   spec.xTiled    = ParseBOOL(val);
            else if (key == "ytiled")   spec.yTiled    = ParseBOOL(val);
        } else {
            auto end = sv.find(' ');
            std::string val{sv.substr(0, end)};
            sv.remove_prefix(end != std::string_view::npos ? end : sv.size());

            if (key == "file") spec.path = val;
        }
    }
    return spec;
}

} // namespace wxui
