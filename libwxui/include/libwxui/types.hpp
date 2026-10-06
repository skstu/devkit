#pragma once
/// libwxui — core type definitions and XML attribute parsing helpers.
/// Mirrors the type system used in docs/libwxui属性列表.xml.

#include <wx/colour.h>
#include <wx/gdicmn.h>   // wxRect, wxSize, wxPoint
#include <wx/string.h>
#include <fmt/format.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

template <>
struct fmt::formatter<wxString> : fmt::formatter<std::string_view> {
    template <typename FormatContext>
    auto format(const wxString& value, FormatContext& ctx) const {
        const wxScopedCharBuffer utf8 = value.ToUTF8();
        const char* data = utf8.data() ? utf8.data() : "";
        return fmt::formatter<std::string_view>::format(data, ctx);
    }
};

namespace wxui {

// ── Attribute storage ────────────────────────────────────────────────────
using AttrMap = std::unordered_map<std::string, std::string>;

/// Normalize XML tag / attribute identifiers so parsing is tolerant of
/// case changes and separators such as '-', '_' and whitespace.
std::string NormalizeXmlIdentifier(std::string_view sv);

/// Convert between wxString and the library's public std::string convention.
/// libwxui stores XML strings as UTF-8 std::string so Chinese text remains
/// stable across Windows, macOS and Linux locale settings.
std::string WxStringToUtf8(const wxString& value);
wxString Utf8ToWxString(std::string_view value);
std::string Utf16ToUtf8(std::u16string_view value);
std::u16string Utf8ToUtf16(std::string_view value);
std::string Utf32ToUtf8(std::u32string_view value);
std::u32string Utf8ToUtf32(std::string_view value);

class String {
public:
    String() = default;
    String(const char* value) : utf8_(value ? value : "") {}
    String(std::string value) : utf8_(std::move(value)) {}
    String(std::string_view value) : utf8_(value) {}
    explicit String(std::u16string_view value) : utf8_(Utf16ToUtf8(value)) {}
    explicit String(std::u32string_view value) : utf8_(Utf32ToUtf8(value)) {}
    explicit String(const wxString& value) : utf8_(WxStringToUtf8(value)) {}

    static String FromUtf8(std::string_view value) { return String(value); }
    static String FromUtf16(std::u16string_view value) { return String(value); }
    static String FromUtf32(std::u32string_view value) { return String(value); }
    static String FromWx(const wxString& value) { return String(value); }

    [[nodiscard]] const std::string& Utf8() const { return utf8_; }
    [[nodiscard]] std::u16string Utf16() const { return Utf8ToUtf16(utf8_); }
    [[nodiscard]] std::u32string Utf32() const { return Utf8ToUtf32(utf8_); }
    [[nodiscard]] wxString Wx() const { return Utf8ToWxString(utf8_); }

    [[nodiscard]] const char* c_str() const { return utf8_.c_str(); }
    [[nodiscard]] bool empty() const { return utf8_.empty(); }
    [[nodiscard]] std::size_t size() const { return utf8_.size(); }
    void clear() { utf8_.clear(); }

    String& operator=(const char* value) {
        utf8_ = value ? value : "";
        return *this;
    }
    String& operator=(std::string value) {
        utf8_ = std::move(value);
        return *this;
    }
    String& operator=(std::string_view value) {
        utf8_ = value;
        return *this;
    }
    String& operator=(const wxString& value) {
        utf8_ = WxStringToUtf8(value);
        return *this;
    }

    String& operator+=(const String& value) {
        utf8_ += value.utf8_;
        return *this;
    }
    String& operator+=(std::string_view value) {
        utf8_ += value;
        return *this;
    }
    String& operator+=(const char* value) {
        if (value) utf8_ += value;
        return *this;
    }

    operator std::string_view() const { return utf8_; }

private:
    std::string utf8_;
};

inline std::string ToUtf8String(const wxString& value) {
    return WxStringToUtf8(value);
}

inline std::string ToUtf8String(const String& value) {
    return value.Utf8();
}

inline wxString FromUtf8String(std::string_view value) {
    return Utf8ToWxString(value);
}

inline String StringFromUtf8(std::string_view value) {
    return String::FromUtf8(value);
}

inline String StringFromWx(const wxString& value) {
    return String::FromWx(value);
}

template <typename... Args>
std::string FormatString(fmt::format_string<Args...> format, Args&&... args) {
    return fmt::format(format, std::forward<Args>(args)...);
}

template <typename... Args>
wxString FormatWx(fmt::format_string<Args...> format, Args&&... args) {
    return Utf8ToWxString(FormatString(format, std::forward<Args>(args)...));
}

template <typename... Args>
String Format(fmt::format_string<Args...> format, Args&&... args) {
    return String::FromUtf8(FormatString(format, std::forward<Args>(args)...));
}

// ── Type parsers ─────────────────────────────────────────────────────────

/// Parse "0xAARRGGBB" or "#RRGGBB" → wxColour.
wxColour ParseDWORD(std::string_view sv, const wxColour& fallback = wxColour());

/// Parse a raw four-value tuple "a,b,c,d" → wxRect(a, b, c, d).
/// The library uses this for both geometry (x,y,w,h) and edge boxes
/// (left,top,right,bottom), so the tuple is preserved as-is.
wxRect ParseRECT(std::string_view sv, const wxRect& fallback = {0, 0, 0, 0});

/// Parse "width,height" → wxSize.
wxSize ParseSIZE(std::string_view sv, const wxSize& fallback = {0, 0});

/// Parse "true"/"false" (case-insensitive) → bool.
bool ParseBOOL(std::string_view sv, bool fallback = false);

/// Parse integer string (may be negative) → int.
int ParseINT(std::string_view sv, int fallback = 0);

// ── AttrMap helper overloads ─────────────────────────────────────────────

inline wxColour AttrColour(const AttrMap& m, std::string_view k,
                            const wxColour& d = {}) {
    if (auto it = m.find(std::string{k}); it != m.end())
        return ParseDWORD(it->second, d);
    return d;
}

inline wxRect AttrRect(const AttrMap& m, std::string_view k,
                       const wxRect& d = {}) {
    if (auto it = m.find(std::string{k}); it != m.end())
        return ParseRECT(it->second, d);
    return d;
}

inline wxSize AttrSize(const AttrMap& m, std::string_view k,
                       const wxSize& d = {}) {
    if (auto it = m.find(std::string{k}); it != m.end())
        return ParseSIZE(it->second, d);
    return d;
}

inline bool AttrBool(const AttrMap& m, std::string_view k, bool d = false) {
    if (auto it = m.find(std::string{k}); it != m.end())
        return ParseBOOL(it->second, d);
    return d;
}

inline int AttrInt(const AttrMap& m, std::string_view k, int d = 0) {
    if (auto it = m.find(std::string{k}); it != m.end())
        return ParseINT(it->second, d);
    return d;
}

inline std::string AttrStr(const AttrMap& m, std::string_view k,
                            std::string d = {}) {
    if (auto it = m.find(std::string{k}); it != m.end())
        return it->second;
    return d;
}

} // namespace wxui

template <>
struct fmt::formatter<wxui::String> : fmt::formatter<std::string_view> {
    template <typename FormatContext>
    auto format(const wxui::String& value, FormatContext& ctx) const {
        return fmt::formatter<std::string_view>::format(value.Utf8(), ctx);
    }
};
