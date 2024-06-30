#if !defined(INC_H___5ABFD307_8EBE_4F10_8105_13616F0B7084__HEAD__)
#define INC_H___5ABFD307_8EBE_4F10_8105_13616F0B7084__HEAD__

#if 0
namespace libconv {
#include <libiconv/source/include/iconv.h>
#pragma comment(lib,"libiconv.lib")
}///namespace libconv
#endif

namespace shared {
 class IConv final {
 public:
  IConv() {}
  ~IConv() = delete;
 public:
  static std::string ToLowerA(const std::string& src) {
   std::string result;
   do {
    if (src.empty())
     break;
    result = src;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
   } while (0);
   return result;
  }
  static std::wstring ToLowerW(const std::wstring& src) {
   std::wstring result;
   do {
    if (src.empty())
     break;
    result = src;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
   } while (0);
   return result;
  }
  static std::string ToUpperA(const std::string& src) {
   std::string result;
   do {
    if (src.empty())
     break;
    result = src;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
   } while (0);
   return result;
  }

  static std::wstring ToUpperW(const std::wstring& src) {
   std::wstring result;
   do {
    if (src.empty())
     break;
    result = src;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
   } while (0);
   return result;
  }

  static std::string WStringToUTF8(const std::wstring& src) {
   std::string result;
   char* pElementText = nullptr;
   do {
    if (src.empty())
     break;
    size_t iTextLen = ::WideCharToMultiByte(CP_UTF8, 0, (LPWSTR)src.data(), -1, NULL, 0, NULL, NULL);
    pElementText = new char[iTextLen + 1];
    memset((void*)pElementText, 0, (iTextLen + 1) * sizeof(char));
    ::WideCharToMultiByte(CP_UTF8, 0, (LPWSTR)src.data(), -1, pElementText, (int)iTextLen, NULL, NULL);
    result = pElementText;
   } while (0);
   if (pElementText) {
    delete[] pElementText;
    pElementText = nullptr;
   }
   return result;
  }

  static std::wstring UTF8ToWString(const std::string& src) {
   std::wstring result;
   wchar_t* pUnicode = nullptr;
   do {
    if (src.empty())
     break;
    size_t unicodeLen = ::MultiByteToWideChar(CP_UTF8, 0, src.data(), -1, NULL, 0);
    pUnicode = new wchar_t[unicodeLen + 1];
    ::memset((void*)pUnicode, 0, (unicodeLen + 1) * sizeof(wchar_t));
    ::MultiByteToWideChar(CP_UTF8, 0, src.data(), -1, (LPWSTR)pUnicode, (int)unicodeLen);
    result = pUnicode;
   } while (0);
   if (pUnicode) {
    delete[] pUnicode;
    pUnicode = nullptr;
   }
   return result;
  }

  static std::string MBytesToUTF8(const std::string& src) {
   std::string result;
   do {
    if (src.empty())
     break;
    auto unicode = MBytesToWString(src);
    if (unicode.empty())
     break;
    result = WStringToUTF8(unicode);
   } while (0);
   return result;
  }

  static std::string UTF8ToMBytes(const std::string& src) {
   std::string result;
   do {
    if (src.empty())
     break;
    auto unicode = UTF8ToWString(src);
    if (unicode.empty())
     break;
    result = WStringToMBytes(unicode);
   } while (0);
   return result;
  }

  static std::string WStringToMBytes(const std::wstring& src, const std::locale& locale = std::locale{ "" }) {
   std::string result;
   char* pElementText = nullptr;
   do {
    if (src.empty())
     break;
    size_t iTextLen = ::WideCharToMultiByte(CP_ACP, 0, src.data(), -1, NULL, 0, NULL, NULL);
    pElementText = new char[iTextLen + 1];
    memset((void*)pElementText, 0, (iTextLen + 1) * sizeof(char));
    ::WideCharToMultiByte(CP_ACP, 0, src.data(), -1, pElementText, (int)iTextLen, NULL, NULL);
    result = pElementText;
   } while (0);
   if (pElementText) {
    delete[] pElementText;
    pElementText = nullptr;
   }
   return result;
  }

  static std::wstring MBytesToWString(const std::string& src, const std::locale& locale = std::locale{ "" }) {
   std::wstring result;
   wchar_t* pUnicode = nullptr;
   do {
    if (src.empty())
     break;
    size_t unicodeLen = ::MultiByteToWideChar(CP_ACP, 0, src.data(), -1, NULL, 0);
    pUnicode = new wchar_t[unicodeLen + 1];
    memset(pUnicode, 0, (unicodeLen + 1) * sizeof(wchar_t));
    ::MultiByteToWideChar(CP_ACP, 0, src.data(), -1, (LPWSTR)pUnicode, (int)unicodeLen);
    result = (wchar_t*)pUnicode;
   } while (0);
   if (pUnicode) {
    delete[] pUnicode;
    pUnicode = nullptr;
   }
   return result;
  }

#if 0
  static std::string unicode_to_gbk(const std::wstring&) {
   return "";
  }
  static std::wstring gbk_to_unicode(const std::string& gbk) {
   std::wstring result;
   libconv::libiconv_t iconobj = nullptr;
   do {
    if (gbk.empty())
     break;
    iconobj = libconv::iconv_open("ucs-2le", "gbk");
    if (!iconobj)
     break;
    size_t dummy = 0;
    libconv::iconv(iconobj, NULL, NULL, NULL, &dummy);
    size_t nGbk = gbk.size();
    size_t nUnicode = nGbk * sizeof(wchar_t);
    result.resize(nUnicode, 0x00);
    char* pGbk = (char*)&gbk[0];
    char* pUnicode = (char*)&result[0];
    size_t nRet = libconv::iconv(iconobj, &pGbk, &nGbk, &pUnicode, &nUnicode);
    if (nRet <= 0) {
     result.clear();
     break;
    }
    auto sks = 0;
    //STRING_RESIZE(result);
   } while (0);
   if (iconobj) {
    libconv::iconv_close(iconobj);
    iconobj = nullptr;
   }
   return result;
  }
#endif


  static std::u16string U8StringToU16String(const std::u8string& u8str) {
   std::u16string result;
   do {
    if (u8str.empty())
     break;
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    result = convert.from_bytes(reinterpret_cast<const char*>(u8str.data()), reinterpret_cast<const char*>(u8str.data() + u8str.size()));
   } while (0);
   return result;
  }
  static std::u8string U32StringToU8String(const std::u32string& u32str) {
   std::u8string result;
   do {
    if (u32str.empty())
     break;
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> convert;
    std::string u8str = convert.to_bytes(u32str);
    result = std::u8string(reinterpret_cast<const char8_t*>(u8str.c_str()), u8str.length());
   } while (0);
   return result;
  }
  static std::u32string U8StringToU32String(const std::u8string& u8str) {
   std::u32string result;
   do {
    if (u8str.empty())
     break;
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
    result = converter.from_bytes(reinterpret_cast<const char*>(u8str.data()),
     reinterpret_cast<const char*>(u8str.data() + u8str.length()));
   } while (0);
   return result;
  }
  static std::u8string U16StringToU8String(const std::u16string& u16str) {
   std::u8string result;
#if 0
   for (char16_t c : u16str) {
    if (c < 0x80) {
     result.push_back(static_cast<char8_t>(c));
    }
    else if (c < 0x800) {
     result.push_back(static_cast<char8_t>((c >> 6) | 0xC0));
     result.push_back(static_cast<char8_t>((c & 0x3F) | 0x80));
    }
    else {
     result.push_back(static_cast<char8_t>((c >> 12) | 0xE0));
     result.push_back(static_cast<char8_t>(((c >> 6) & 0x3F) | 0x80));
     result.push_back(static_cast<char8_t>((c & 0x3F) | 0x80));
    }
   }
#else
   do {
    if (u16str.empty())
     break;
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::string u8str = convert.to_bytes(u16str);
    result = std::u8string(reinterpret_cast<const char8_t*>(u8str.c_str()), u8str.length());
   } while (0);

#endif
   return result;
  }

  static std::u32string U16StringToU32String(const std::u16string& u16str) {
   std::u32string result;
   do {
    if (u16str.empty())
     break;
    std::u8string u8str = U16StringToU8String(u16str);
    result = U8StringToU32String(u8str);
   } while (0);
   return result;
  }
  static std::u16string U32StringToU16String(const std::u32string& u32str) {
   std::u16string result;
   do {
    if (u32str.empty())
     break;
    std::u8string u8str = U32StringToU8String(u32str);
    result = U8StringToU16String(u8str);
   } while (0);
   return result;
  }





  static std::wstring U8StringToWString(const std::u8string& u8str) {
   std::wstring result;
   do {
    if (u8str.empty())
     break;
    int requiredSize = ::MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(u8str.c_str()), -1, nullptr, 0);
    if (requiredSize == 0)
     break;
    result.resize(requiredSize - 1);
    if (::MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(u8str.c_str()), -1, &result[0], requiredSize) == 0)
     break;
   } while (0);
   return result;
  }

  static std::string U8StringToString(const std::u8string& u8str) {
   std::string result;
   do {
    if (u8str.empty())
     break;
    std::wstring wstr = U8StringToWString(u8str);
    result = WStringToMBytes(wstr);
   } while (0);
   return result;
  }


  static std::u8string WStringToU8String(const std::wstring& wstr) {
   std::u8string result;
   do {
    if (wstr.empty())
     break;
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
    std::string utf8str = convert.to_bytes(wstr);
    result = std::u8string(reinterpret_cast<const char8_t*>(utf8str.c_str()), utf8str.length());
   } while (0);
   return result;
  }

  static std::u8string StringToU8String(const std::string& str) {
   std::u8string result;
   do {
    if (str.empty())
     break;
    std::wstring wstr = MBytesToWString(str);
    result = WStringToU8String(wstr);
   } while (0);
   return result;
  }

  static std::string WStringToString(const std::wstring& wstr) {
   std::string result;
   do {
    if (wstr.empty())
     break;
    result = WStringToMBytes(wstr);
   } while (0);
   return result;
  }

  static std::wstring StringToWString(const std::string& str) {
   std::wstring result;
   do {
    if (str.empty())
     break;
    result = MBytesToWString(str);
   } while (0);
   return result;
  }

  static std::wstring U16StringToWString(const std::u16string& u16str) {
   std::wstring result;
   do {
    if (u16str.empty())
     break;
    std::u8string u8str = U16StringToU8String(u16str);
    result = U8StringToWString(u8str);
   } while (0);
   return result;
  }

  static std::string U16StringToString(const std::u16string& u16str) {
   std::string result;
   do {
    if (u16str.empty())
     break;
    std::wstring wstr = U16StringToWString(u16str);
    result = WStringToString(wstr);
   } while (0);
   return result;
  }

  static std::wstring U32StringToWString(const std::u32string& u32str) {
   std::wstring result;
   do {
    if (u32str.empty())
     break;
    std::u8string u8str = U32StringToU8String(u32str);
    result = U8StringToWString(u8str);
   } while (0);
   return result;
  }

  static std::string U32StringToString(const std::u32string& u32str) {
   std::string result;
   do {
    if (u32str.empty())
     break;
    std::u8string u8str = U32StringToU8String(u32str);
    result = U8StringToString(u8str);
   } while (0);
   return result;
  }

  static std::u32string StringToU32String(const std::string& str) {
   std::u32string result;
   do {
    if (str.empty())
     break;
    std::u8string u8str = StringToU8String(str);
    result = U8StringToU32String(u8str);
   } while (0);
   return result;
  }

  static std::u32string WStringToU32String(const std::wstring& str) {
   std::u32string result;
   do {
    if (str.empty())
     break;
    std::u8string u8str = WStringToU8String(str);
    result = U8StringToU32String(u8str);
   } while (0);
   return result;
  }

  static std::u16string StringToU16String(const std::string& str) {
   std::u16string result;
   do {
    if (str.empty())
     break;
    std::u8string u8str = StringToU8String(str);
    result = U8StringToU16String(u8str);
   } while (0);
   return result;
  }

  static std::u16string WStringToU16String(const std::wstring& str) {
   std::u16string result;
   do {
    if (str.empty())
     break;
    std::u8string u8str = WStringToU8String(str);
    result = U8StringToU16String(u8str);
   } while (0);
   return result;
  }

 };
}

#if 0//!@ Demo by 2023.6.29
const auto u8string_test = u8"你好啊，abck,1234,!#%&#$%&.";

std::u16string u16string = shared::IConv::U8StringToU16String(u8string_test);
std::u8string u8string = shared::IConv::U16StringToU8String(u16string);
std::u32string u32string = shared::IConv::U8StringToU32String(u8string);
u8string = shared::IConv::U32StringToU8String(u32string);
u32string = shared::IConv::U16StringToU32String(u16string);
u16string = shared::IConv::U32StringToU16String(u32string);

std::wstring wstr = shared::IConv::U8StringToWString(u8string);
std::string str = shared::IConv::U8StringToString(u8string);
u8string = shared::IConv::WStringToU8String(wstr);
u8string = shared::IConv::StringToU8String(str);

u16string = shared::IConv::WStringToU16String(wstr);
u32string = shared::IConv::WStringToU32String(wstr);
wstr = shared::IConv::U16StringToWString(u16string);
str = shared::IConv::U16StringToString(u16string);


#endif

/// /*新生®（上海）**/
/// /*2022_02_19T09:06:04.6804495Z**/
/// /*_ _ _ _ _ _ _ www.skstu.com _ _ _ _ _ _ _**/
#endif ///INC_H___5ABFD307_8EBE_4F10_8105_13616F0B7084__HEAD__