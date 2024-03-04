#include "stdafx.h"
using namespace std;
namespace fs = filesystem;
string Path::FixA(const string &input_path, const string &path_spilt) {
  string result = input_path;
  do {
    if (!VerifyA(result) || path_spilt.empty())
      break;
    for (auto it = result.begin(); it != result.end();) {
      if (*it == '\\' || *it == '/') {
        *it = *path_spilt.begin();
      }
      if (it == result.begin()) {
        ++it;
        continue;
      }
      if (*prev(it) == *it && *it == *path_spilt.begin()) {
        it = result.erase(it);
        continue;
      }
      ++it;
    }
  } while (0);
  return result;
}
wstring Path::FixW(const wstring &input_path, const wstring &path_spilt) {
  wstring result = input_path;
  do {
    if (!VerifyW(result) || path_spilt.empty())
      break;
    for (auto it = result.begin(); it != result.end();) {
      if (*it == L'\\' || *it == L'/') {
        *it = *path_spilt.begin();
      }
      if (it == result.begin()) {
        ++it;
        continue;
      }
      if (*prev(it) == *it && *it == *path_spilt.begin()) {
        it = result.erase(it);
        continue;
      }
      ++it;
    }
  } while (0);
  return result;
}
string Path::ParentA(const string &path) {
  string result;
#ifdef _STL_HAS_CXX17
  do {
    if (path.empty())
      break;
    fs::path fullPath(path);
    fs::path parentPath = fullPath.parent_path();
    result = parentPath.string();
  } while (0);
#endif
  return result;
}
wstring Path::ParentW(const wstring &path) {
  wstring result;
#ifdef _STL_HAS_CXX17
  do {
    if (path.empty())
      break;
    fs::path fullPath(path);
    fs::path parentPath = fullPath.parent_path();
    result = parentPath.wstring();
  } while (0);
#endif
  return result;
}
string Path::AppendA(const string &abs, const string &rel) {
  string result;
  do {
    if (rel.empty())
      break;
    string tmp = abs + "/" + rel;
    fs::path path = tmp;
    result = path.lexically_normal().string();
  } while (0);
  return result;
}
wstring Path::AppendW(const wstring &abs, const wstring &rel) {
  wstring result;
  do {
    if (rel.empty())
      break;
    wstring tmp = abs + L"/" + rel;
    fs::path path = tmp;
    result = path.lexically_normal().wstring();
  } while (0);
  return result;
}
string Path::PathnameToPathA(const string &input_pathname) {
  string result = input_pathname;
  do {
    if (result.empty())
      break;
    if (!VerifyA(result))
      break;
    for (auto rit = result.rbegin(); rit != result.rend(); ++rit) {
      if (*rit != '\\' && *rit != '/') {
        result.resize(distance(result.begin(), rit.base()) - 1);
        continue;
      }
      break;
    }
  } while (0);
  return result;
}
wstring Path::PathnameToPathW(const wstring &input_pathname) {
  wstring result = input_pathname;
  do {
    if (result.empty())
      break;
    if (!VerifyW(result))
      break;
    for (auto rit = result.rbegin(); rit != result.rend(); ++rit) {
      if (*rit != L'\\' && *rit != L'/') {
        result.resize(distance(result.begin(), rit.base()) - 1);
        continue;
      }
      break;
    }
  } while (0);
  return result;
}
bool Path::ExistsA(const string &input_path) {
  bool result = false;
  do {
#ifdef _STL_HAS_CXX17
    if (input_path.empty())
      break;
    fs::path path = input_path;
    if (!fs::exists(path))
      break;
    result = true;
#endif
  } while (0);
  return result;
}
bool Path::ExistsW(const wstring &input_path) {
  bool result = false;
  do {
#ifdef _STL_HAS_CXX17
    if (input_path.empty())
      break;
    fs::path path = input_path;
    if (!fs::exists(path))
      break;
    result = true;
#endif
  } while (0);
  return result;
}
bool Path::VerifyA(const string &path) {
  bool result = false;
  do {
    if (path.empty())
      break;
    if (path.find('/') == string::npos && path.find('\\') == string::npos)
      break;
    result = true;
  } while (0);
  return result;
}
bool Path::VerifyW(const wstring &path) {
  bool result = false;
  do {
    if (path.empty())
      break;
    if (path.find(L'/') == wstring::npos && path.find(L'\\') == wstring::npos)
      break;
    result = true;
  } while (0);
  return result;
}
string Path::NormalA(const string &path) {
  string result;
  do {
    if (path.empty())
      break;
#ifdef _STL_HAS_CXX17
    fs::path _path = path;
    result = _path.lexically_normal().string();
#endif
  } while (0);
  return result;
}
wstring Path::NormalW(const wstring &path) {
  wstring result;
  do {
    if (path.empty())
      break;
#ifdef _STL_HAS_CXX17
    fs::path _path = path;
    result = _path.lexically_normal().wstring();
#endif
  } while (0);
  return result;
}