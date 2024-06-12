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
} // namespace sk

#endif //__SKPP_H_