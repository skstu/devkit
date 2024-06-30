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
  requires std::is_convertible_v<T, std::chrono::milliseconds> ||
           std::is_convertible_v<T, std::chrono::minutes> ||
           std::is_convertible_v<T, std::chrono::microseconds>
#endif
  static time_t TimeStamp() {
    return std::chrono::duration_cast<T>(
               std::chrono::time_point_cast<T>(std::chrono::system_clock::now())
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
  static std::vector<std::string> StringSpilt(const std::string &,
                                              const std::string &);
  static std::vector<std::wstring> WStringSplit(const std::wstring &,
                                                const std::wstring &);
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
  static std::string FixA(const std::string &input_path,
                          const std::string &path_spilt);
  static std::wstring FixW(const std::wstring &input_path,
                           const std::wstring &path_spilt);
  static std::string ParentA(const std::string &);
  static std::wstring ParentW(const std::wstring &);
  static std::string AppendA(const std::string &abs, const std::string &rel);
  static std::wstring AppendW(const std::wstring &abs, const std::wstring &rel);
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
           const std::string &, std::vector<char> &,
           const int &mode_ = std::ios::in | std::ios::binary);
  static bool WriteFile(const std::string &, const std::string &,
                        const int &mode_ = static_cast<int>(std::ios::binary) |
                                           static_cast<int>(std::ios::out) |
                                           static_cast<int>(std::ios::trunc));
  static bool WriteFileAddto(const std::string &, const std::string &);
};

namespace container {
class base {
public:
  base() {
  }
  virtual ~base() {
  }

protected:
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
  std::shared_ptr<std::condition_variable> condition_variable_ =
      std::make_shared<std::condition_variable>();
};

template <typename T> class queue final : public base {
public:
  queue(const size_t &_max = 0) : max_(_max) {
  }
  virtual ~queue() {
  }

private:
  void __onsize() {
    do {
      if (max_ <= 0)
        break;
      const size_t total = q_.size();
      std::cout << total << std::endl;
      if (total <= max_)
        break;
      q_.pop();
    } while (0);
  }

public:
  void push(const T &data) {
    std::lock_guard<std::mutex> _lock(*mutex_);
    __onsize();
    q_.push(data);
  }
  void push(const std::vector<T> &datas) {
    std::lock_guard<std::mutex> _lock(*mutex_);
    for (const auto &data : datas) {
      __onsize();
      q_.push(data);
    }
  }
  std::shared_ptr<T> pop_front() {
    std::shared_ptr<T> result;
    std::lock_guard<std::mutex> _lock(*mutex_);
    if (!q_.empty()) {
      result = std::make_shared<T>(q_.front());
      q_.pop();
    }
    return result;
  }
  std::shared_ptr<T> pop_back() {
    std::shared_ptr<T> result;
    std::lock_guard<std::mutex> _lock(*mutex_);
    if (!q_.empty()) {
      result = std::make_shared<T>(q_.back());
      q_.pop();
    }
    return result;
  }
  std::vector<T> pops() {
    std::vector<T> result;
    std::lock_guard<std::mutex> _lock(*mutex_);
    while (!q_.empty()) {
      result.emplace_back(q_.front());
      q_.pop();
    }
    return result;
  }
  bool empty() const {
    bool result = false;
    std::lock_guard<std::mutex> _lock(*mutex_);
    result = q_.empty();
    return result;
  }
  size_t size() const {
    size_t result = 0;
    std::lock_guard<std::mutex> _lock(*mutex_);
    result = q_.size();
    return result;
  }
  void clear() {
    std::lock_guard<std::mutex> _lock(*mutex_);
    std::queue<T> empty;
    std::swap(q_, empty);
  }

private:
  std::queue<T> q_;
  const size_t max_;
};

template <typename K, typename V> class map final : public base {
public:
  map() {
  }
  virtual ~map() {
  }

public:
  void push(const K &k, const V &v) {
    std::lock_guard<std::mutex> _lock(*mutex_);
    auto found = map_.find(k);
    if (found != map_.end())
      map_.erase(found);
    map_.emplace(k, v);
  }
  void push(const K &k, const V &v,
            const std::function<void(const V &)> &exists_cb) {
    std::lock_guard<std::mutex> _lock(*mutex_);
    auto found = map_.find(k);
    if (found != map_.end()) {
      exists_cb(found->second);
      map_.erase(found);
    }
    map_.emplace(k, v);
  }
  std::shared_ptr<V> search(const K &k) const {
    std::shared_ptr<V> result = nullptr;
    std::lock_guard<std::mutex> _lock(*mutex_);
    auto found = map_.find(k);
    if (found != map_.end())
      result = std::make_shared<V>(found->second);
    return result;
  }
  void clear() {
    std::lock_guard<std::mutex> _lock(*mutex_);
    map_.clear();
  }
  size_t size() const {
    std::lock_guard<std::mutex> _lock(*mutex_);
    return map_.size();
  }

private:
  std::map<K, V> map_;
};

template <typename T> class set final : public base {
public:
  set() {
  }
  virtual ~set() {
  }
  void operator=(const std::set<T> &stlset) {
    std::lock_guard<std::mutex> lock(*mutex_);
    m_set = stlset;
  }
  void operator=(const set<T> &skset) {
    std::lock_guard<std::mutex> lock(*mutex_);
    m_set = skset.m_set;
  }

public:
  std::shared_ptr<T> begin() const {
    std::shared_ptr<T> result;
    std::lock_guard<std::mutex> lock(*mutex_);
    if (!m_set.empty()) {
      return std::make_shared<T>(*m_set.begin());
    }
    return result;
  }
  std::shared_ptr<T> end() const {
    std::shared_ptr<T> result;
    std::lock_guard<std::mutex> lock(*mutex_);
    if (!m_set.empty()) {
      return std::make_shared<T>(*std::prev(m_set.end()));
    }
    return result;
  }
  std::shared_ptr<T> pop_begin() {
    std::shared_ptr<T> result;
    std::lock_guard<std::mutex> lock(*mutex_);
    if (!m_set.empty()) {
      auto itTarget = m_set.begin();
      result = std::make_shared<T>(*itTarget);
      m_set.erase(itTarget);
    }
    return result;
  }
  std::shared_ptr<T> pop_end() {
    std::shared_ptr<T> result;
    std::lock_guard<std::mutex> lock(*mutex_);
    if (!m_set.empty()) {
      auto itTarget = std::prev(m_set.end());
      result = std::make_shared<T>(*itTarget);
      m_set.erase(itTarget);
    }
    return result;
  }
  //! 签名 : push
  //! 说明 :
  //! 注意 : return true : 存在 false 不存在
  //! 日期 : Sat Jun 13 20:11:38 UTC+0800 2020
  bool push(const T &data) {
    bool result = false;
    std::lock_guard<std::mutex> lock(*mutex_);
    if (m_set.find(data) != m_set.end()) {
      m_set.erase(data);
      result = true;
    }
    m_set.insert(data).second;
    return result;
  }

  //! 签名 : pushpush
  //! 说明 :
  //! 注意 : 存在回调
  //! 日期 : Sat Jun 13 17:55:07 UTC+0800 2020
  bool pushpush(const T &data, const std::function<void(T &)> &pushpush_cb) {
    bool result = false;
    std::lock_guard<std::mutex> lock(*mutex_);
    auto find = m_set.find(data);
    if (find != m_set.end()) {
      auto val = *find;
      m_set.erase(find);
      pushpush_cb(val);
      result = m_set.insert(val).second;
    } else {
      result = m_set.insert(data).second;
    }
    return result;
  }
  bool pop(const T &key) {
    bool result = false;
    std::lock_guard<std::mutex> lock(*mutex_);
    auto find = m_set.find(key);
    if (find != m_set.end()) {
      m_set.erase(find);
      result = true;
    }
    return result;
  }
  bool empty() const {
    bool result = false;
    std::lock_guard<std::mutex> lock(*mutex_);
    result = m_set.empty();
    return result;
  }
  size_t size() const {
    size_t result = 0;
    std::lock_guard<std::mutex> lock(*mutex_);
    result = m_set.size();
    return result;
  }
  void clear() {
    std::lock_guard<std::mutex> lock(*mutex_);
    m_set.clear();
  }
  void clearat(const std::function<void(const T &, bool &)> &iterate_cb) {
    std::lock_guard<std::mutex> lock(*mutex_);
    bool isClear = false;
    for (auto it = m_set.begin(); it != m_set.end();) {
      isClear = false;
      iterate_cb((T &)(*it), isClear);
      if (true == isClear) {
        it = m_set.erase(it);
      } else {
        ++it;
      }
    }
  }
  bool exists(const T &key) const {
    std::lock_guard<std::mutex> lock(*mutex_);
    return m_set.find(key) != m_set.end();
  }
  std::shared_ptr<T> search(const T &key) {
    std::shared_ptr<T> result;
    std::lock_guard<std::mutex> lock(*mutex_);
    if (!m_set.empty()) {
      auto find = m_set.find(key);
      if (find != m_set.end()) {
        return std::make_shared<T>(*find);
      }
    }
    return result;
  }
  bool search(const T &key, const std::function<void(const T &)> &search_cb) {
    bool result = false;
    std::lock_guard<std::mutex> lock(*mutex_);
    auto find = m_set.find(key);
    if (find != m_set.end()) {
      search_cb(*find);
      result = true;
    }
    return result;
  }
  bool search_noconst(const T &key, const std::function<void(T &)> &search_cb) {
    bool result = false;
    std::lock_guard<std::mutex> lock(*mutex_);
    auto find = m_set.find(key);
    if (find != m_set.end()) {
      search_cb(*find);
      result = true;
    }
    return result;
  }
  bool search(const T &key) const {
    bool result = false;
    std::lock_guard<std::mutex> lock(*mutex_);
    auto find = m_set.find(key);
    if (find != m_set.end()) {
      result = true;
    }
    return result;
  }
  void iterate(const std::function<void(T &)> &iteratecb) {
    std::lock_guard<std::mutex> lock(*mutex_);
    for (auto it = m_set.begin(); it != m_set.end(); ++it) {
      iteratecb((T &)(*it));
    }
  }
  void
  iterate_const(const std::function<void(const T &, bool &)> &iteratecb) const {
    std::lock_guard<std::mutex> lock(*mutex_);
    bool itbreak = false;
    for (auto it = m_set.begin(); it != m_set.end(); ++it) {
      itbreak = false;
      iteratecb((T &)(*it), itbreak);
      if (itbreak)
        break;
    }
  }
  void iterate(const std::function<void(T &, bool &)> &iteratecb) {
    std::lock_guard<std::mutex> lock(*mutex_);
    bool itbreak = false;
    for (auto it = m_set.begin(); it != m_set.end(); ++it) {
      iteratecb((T &)(*it), itbreak);
      if (itbreak == true) {
        break;
      }
    }
  }
  void iterate(const std::function<void(const T &, bool &)> &iteratecb) const {
    std::lock_guard<std::mutex> lock(*mutex_);
    bool itbreak = false;
    for (auto it = m_set.begin(); it != m_set.end(); ++it) {
      iteratecb(*it, itbreak);
      if (itbreak == true) {
        break;
      }
    }
  }
  std::vector<T> Vector() const {
    std::vector<T> result;
    std::lock_guard<std::mutex> lock(*mutex_);
    for (const auto &node : m_set) {
      result.emplace_back(node);
    }
    return result;
  }
  std::set<T> Source() {
    std::set<T> result;
    std::lock_guard<std::mutex> lock(*mutex_);
    for (const auto &node : m_set) {
      result.insert(node);
    }
    return result;
  }

private:
  std::set<T> m_set;
};

} // namespace container

} // namespace stl

/// /*新生®（上海）**/
/// /*2022_02_19T06:18:54.5642649Z**/
/// /*_ _ _ _ _ _ _ www.skstu.com _ _ _ _ _ _ _**/
#endif /// __STL_H_
