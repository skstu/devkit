#ifndef __STL_H_
#define __STL_H_
/**
 * @file stl.h
 *
 * @brief Only C++ STL functionality is encapsulated.
 *
 * @author Martell
 * @date 2023-11-13
 * @location SHE(ISO 3166-1)
 */

#define _PLATFORM_STL_VER __cplusplus

#ifdef _WIN32
// #define _CRT_SECURE_NO_WARNINGS
#if _MSC_VER
#undef _PLATFORM_STL_VER
#define _PLATFORM_STL_VER _MSVC_LANG
#endif
#endif

#define _STL_HAS_CXX03 0
#define _STL_HAS_CXX11 0
#define _STL_HAS_CXX14 0
#define _STL_HAS_CXX17 0
#define _STL_HAS_CXX20 0
#define _STL_HAS_CXX23 0

#if _PLATFORM_STL_VER >= 199711L
#undef _STL_HAS_CXX03
#define _STL_HAS_CXX03 1
#endif

#if _PLATFORM_STL_VER >= 201103L
#undef _STL_HAS_CXX11
#define _STL_HAS_CXX11 1
#endif

#if _PLATFORM_STL_VER >= 201402L
#undef _STL_HAS_CXX14
#define _STL_HAS_CXX14 1
#endif

#if _PLATFORM_STL_VER >= 201703L
#undef _STL_HAS_CXX17
#define _STL_HAS_CXX17 1
#endif

#if _PLATFORM_STL_VER >= 202002L
#undef _STL_HAS_CXX20
#define _STL_HAS_CXX20 1
#endif

#if _PLATFORM_STL_VER > 202002L
#undef _STL_HAS_CXX23
#define _STL_HAS_CXX23 1
#endif

#if _STL_HAS_CXX03
#include <fcntl.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#endif

#if _STL_HAS_CXX11
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <random>
#include <regex>
#include <thread>
#include <tuple>
#include <typeinfo>
#endif

#if _STL_HAS_CXX14
#include <any>
#include <bitset>
#include <deque>
#include <future>
#include <optional>
#include <string_view>
#include <variant>
#endif

#if _STL_HAS_CXX17
#include <filesystem>
#endif

#if _STL_HAS_CXX20
#include <concepts>
#include <coroutine>
#if _WIN32 && _MSC_VER
#include <format>
#endif
#endif

#if _STL_HAS_CXX23
#include <span> //(proposed for C++23, but may be available in some compilers)
#endif

namespace stl {
class Time {
  public:
    template <typename T = std::chrono::seconds>
#if _STL_HAS_CXX20
        requires std::is_convertible_v<T, std::chrono::milliseconds>
                 || std::is_convertible_v<T, std::chrono::minutes>
                 || std::is_convertible_v<T, std::chrono::microseconds>
#endif
    static time_t TimeStamp() {
        return std::chrono::duration_cast<T>(
                   std::chrono::time_point_cast<T>(
                       std::chrono::system_clock::now())
                       .time_since_epoch())
            .count();
    }
    static std::string CurrentDateA();
    static std::wstring CurrentDateW();
};

class String {
  public:
    static std::string LowerA(const std::string &);
    static std::wstring LowerW(const std::wstring &);
    static std::string UpperA(const std::string &);
    static std::wstring UpperW(const std::wstring &);
    static std::string BinaryToHexString(const std::string &s);
    static std::string HexStringToBinary(const std::string &s);
    static std::vector<std::string>
    StringSpilt(const std::string &, const std::string &);
    static std::vector<std::wstring>
    WStringSplit(const std::wstring &, const std::wstring &);
    /*
     * Input IPV4: "192.168.0.1:8888"
     * Input IPV6: "[::]:8888"
     */
    static std::tuple<std::string, int> UnmadeIPAddr(const std::string &addr);
    static std::string MadeIPAddr(const std::string &ip, const int &port);
    static std::string UrlFixA(const std::string &inputUrlOrPath);
    static std::string WemadeEncode(const std::string &);
    static std::string WemadeDecode(const std::string &);
};

class Directory {
  public:
    //!@ input example : c:\\Windows\\System32\\
  //!
    static bool CreateA(const std::string &);
    static bool CreateW(const std::wstring &);
    static bool RemoveAllA(const std::string &);
    static bool RemoveAllW(const std::wstring &);
};

class Path {
  public:
    static std::string NormalA(const std::string &);
    static std::wstring NormalW(const std::wstring &);
    static bool ExistsA(const std::string &input_path);
    static bool ExistsW(const std::wstring &input_path);
    static bool VerifyA(const std::string &input_path);
    static bool VerifyW(const std::wstring &input_path);
    static std::string
    FixA(const std::string &input_path, const std::string &path_spilt);
    static std::wstring
    FixW(const std::wstring &input_path, const std::wstring &path_spilt);
    static std::string ParentA(const std::string &);
    static std::wstring ParentW(const std::wstring &);
    static std::string AppendA(const std::string &abs, const std::string &rel);
    static std::wstring
    AppendW(const std::wstring &abs, const std::wstring &rel);
    static std::string PathnameToPathA(const std::string &input_pathname);
    static std::wstring PathnameToPathW(const std::wstring &input_pathname);
};

class File {
  public:
    static std::string Create(const std::string &);
    static bool RemoveA(const std::string &);
    static bool RemoveW(const std::wstring &);
    static std::string
    ReadFile(/*std::ios::_Nocreate | std::ios::_Noreplace | std::ios::binary*/
             const std::string &,
             const int &mode_ = std::ios::in | std::ios::binary);
    static void
    ReadFile(/*std::ios::_Nocreate | std::ios::_Noreplace | std::ios::binary*/
             const std::string &,
             std::vector<char> &,
             const int &mode_ = std::ios::in | std::ios::binary);
    static bool WriteFile(
        const std::string &,
        const std::string &,
        const int &mode_ = static_cast<int>(std::ios::binary)
                           | static_cast<int>(std::ios::out)
                           | static_cast<int>(std::ios::trunc));
    static bool WriteFileAddto(const std::string &, const std::string &);
};
} // namespace stl

/// /*新生®（上海）**/
/// /*2022_02_19T06:18:54.5642649Z**/
/// /*_ _ _ _ _ _ _ www.skstu.com _ _ _ _ _ _ _**/
#endif /// __STL_H_
