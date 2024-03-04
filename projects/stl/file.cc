#include "stdafx.h"
using namespace std;
namespace fs = std::filesystem;

bool File::RemoveA(const string &file) {
  bool result = false;
#ifdef _STL_HAS_CXX17
  do {
    if (file.empty())
      break;
    fs::path file_path = file;
    if (fs::is_directory(file_path))
      break;
    result = fs::remove(file_path);
  } while (0);
#endif
  return result;
}
bool File::RemoveW(const wstring &file) {
  bool result = false;
#ifdef _STL_HAS_CXX17
  do {
    if (file.empty())
      break;
    fs::path file_path = file;
    if (fs::is_directory(file_path))
      break;
    result = fs::remove(file_path);
  } while (0);
#endif
  return result;
}

string File::Create(const string &file_path) {
  string result;
#ifdef _STL_HAS_CXX17
  do {
    if (file_path.empty())
      break;
    const fs::path path = file_path;
    string _tmp = path.lexically_normal().string();
    if (fs::exists(path)) {
      if (!fs::is_directory(path)) {
        result = _tmp;
        break;
      }
    }
    const fs::path parent_path = path.parent_path();
    if (!fs::exists(parent_path)) {
      if (!fs::create_directories(parent_path.lexically_normal()))
        break;
    }
    fstream of(_tmp, static_cast<ios_base::openmode>(ios::binary | ios::out |
                                                     ios::app));
    if (!of.is_open())
      break;
    of.close();
    result = _tmp;
  } while (0);
#endif
  return result;
}
string File::ReadFile(/*ios::_Nocreate | ios::_Noreplace | ios::binary*/
                      const string &file_,
                      const int &mode_ /*= ios::in | ios::binary*/) {
  string result;
  fstream of(file_, static_cast<ios_base::openmode>(mode_));
  do {
    if (!of.is_open())
      break;
    of.seekg(0, of.end);
    size_t size = static_cast<size_t>(of.tellg());
    if (size <= 0)
      break;
    result.resize(size, 0x00);
    of.seekg(0, of.beg);
    of.read(&result[0], size);
  } while (0);
  if (of.is_open())
    of.close();
  return result;
}

void File::ReadFile(/*ios::_Nocreate | ios::_Noreplace | ios::binary*/
                    const string &file_, vector<char> &out_,
                    const int &mode_ /*= ios::in | ios::binary*/) {
  out_.clear();
  fstream of(file_, static_cast<ios_base::openmode>(mode_));
  do {
    if (!of.is_open())
      break;
    of.seekg(0, of.end);
    size_t size = static_cast<size_t>(of.tellg());
    if (size <= 0)
      break;
    out_.resize(size, 0x00);
    of.seekg(0, of.beg);
    of.read(&out_[0], size);
  } while (0);
  if (of.is_open())
    of.close();
}
bool File::WriteFile(const string& file_, const string& data_,
               const int& mode_ /*= static_cast<int>(ios::binary) |
                                  static_cast<int>(ios::out) |
                                  static_cast<int>(ios::trunc)*/)
{
  bool result = false;
  do {
    if (data_.empty())
      break;
#if 0
   if (!Win::AccessA(Win::GetPathByPathnameA(FilePathname)))
    Win::CreateDirectoryA(Win::GetPathByPathnameA(FilePathname));
#endif
    fstream of(file_, static_cast<ios_base::openmode>(mode_));
    if (!of.is_open())
      break;
    of << data_;
    of.close();
    result = true;
  } while (0);
  return result;
}

bool File::WriteFileAddto(const string &file_, const string &data_) {
  bool result = false;
  do {
    if (data_.empty())
      break;
#if 0
   if (!Win::AccessA(FilePathname))
    Win::CreateDirectoryA(Win::GetPathByPathnameA(FilePathname));
#endif
    ofstream of(file_, ios::binary | ios::out | ios::app);
    if (!of.is_open())
      break;
    of << data_;
    of.close();
    result = true;
  } while (0);
  return result;
}