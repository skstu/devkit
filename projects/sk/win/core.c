#include <stdafx.h>

int sk_get_pid(long *pid) {
  *pid = (long)GetCurrentProcessId();
  return SK_OK;
}
int sk_get_exepath(char **exepath, size_t *len) {
  int r = SK_NO;
  *exepath = NULL;
  *len = 0;
  do {
    char path[_MAX_PATH] = {0};
    *len = GetModuleFileNameA(NULL, path, _MAX_PATH);
    if (*len <= 0)
      break;
    *exepath = sk__strdup(path);
    (*exepath)[*len] = 0;
    r = SK_OK;
  } while (0);
  return r;
}