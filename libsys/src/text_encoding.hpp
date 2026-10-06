#pragma once

#include <codecvt>
#include <locale>
#include <string>

class IConv final {
public:
  static std::string UTF16ToUTF8(const std::u16string &value) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    return converter.to_bytes(value);
  }

#if defined(_WIN32)
  static std::wstring UTF8ToWide(const std::string &value) {
    if (value.empty())
      return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0)
      return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
  }

  static std::wstring UTF16ToWide(const std::u16string &value) {
    return {value.begin(), value.end()};
  }

  static std::string WideToUTF8(const std::wstring &value) {
    if (value.empty())
      return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
      return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(),
                        static_cast<int>(value.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
  }
#endif
};