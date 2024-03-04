#include "stdafx.h"
using namespace std;
namespace fs = std::filesystem;
bool Directory::RemoveAllA(const std::string &dir) {
  bool result = false;
  do {
    fs::path path = dir;
    if (!fs::is_directory(path))
      break;
    result = fs::remove_all(path);
  } while (0);
  return result;
}
bool Directory::RemoveAllW(const std::wstring &dir) {
  bool result = false;
  do {
    fs::path path = dir;
    if (!fs::is_directory(path))
      break;
    result = fs::remove_all(path);
  } while (0);
  return result;
}
bool Directory::CreateA(const string &path) {
  bool result = false;
  do {
    if (path.empty())
      break;
#ifdef _STL_HAS_CXX17
    fs::path _path = path;
    if (fs::exists(_path)) {
      result = true;
      break;
    }
    result = fs::create_directories(_path.lexically_normal());
#endif
  } while (0);
  return result;
}
bool Directory::CreateW(const wstring &path) {
  bool result = false;
  do {
    if (path.empty())
      break;
#ifdef _STL_HAS_CXX17
    fs::path _path = path;
    if (fs::exists(_path)) {
      result = true;
      break;
    }
    result = fs::create_directories(_path.lexically_normal());
#endif
  } while (0);
  return result;
}