#ifndef __SKPP_H_
#define __SKPP_H_
#include "stl.h"
#include "sk.h"
//!@ The methods in this file need to rely on the 'SK' module.
namespace sk {
static std::string GetCurrentPath() {
  std::string result;
  char *pathname = nullptr;
  char *path = nullptr;
  size_t len = 0;
  do {
    if (SK_OK != sk_get_exepath(&pathname, &len))
      break;
    if (SK_OK != sk_pathname_to_path(pathname, &path, &len))
      break;
    result.append(path, len);
  } while (0);
  sk_mem_free((void **)&pathname);
  sk_mem_free((void **)&path);
  return result;
}

static std::string StringToU8String(const std::string &input) {
  std::string result;
  char *to_name = nullptr;
  size_t to_nameLen = 0;
  char *res = nullptr;
  size_t resLen = 0;
  do {
    if (input.empty())
      break;
    sk_icu_match_type_to_name(sk_icu_conv_type::ICU_CONV_TYPE_UTF8, &to_name,
                              &to_nameLen);
    sk_icu_convert(input.data(), input.size(), to_name, &res, &resLen);
    if (!res)
      break;
    result.append(res, resLen);
  } while (0);
  sk_mem_free((void **)&to_name);
  sk_mem_free((void **)&res);
  return result;
}
static std::string WStringToString(const std::wstring &input) {
  std::string result;
  char *to_name = nullptr;
  size_t to_nameLen = 0;
  char *res = nullptr;
  size_t resLen = 0;
  do {
    if (input.empty())
      break;
    sk_icu_match_type_to_name(sk_icu_conv_type::ICU_CONV_TYPE_ASCII, &to_name,
                              &to_nameLen);
    sk_icu_convert((char *)input.data(), input.size() * sizeof(wchar_t),
                   to_name, &res, &resLen);
    if (!res)
      break;
    result.append(res, resLen);
  } while (0);
  sk_mem_free((void **)&to_name);
  sk_mem_free((void **)&res);
  return result;
}

} // namespace sk

#endif //__SKPP_H_