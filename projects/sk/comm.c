#include <stdafx.h>

int sk_pathname_to_path(const char *pathname, char **path, size_t *len) {
  int r = SK_NO;
  *path = NULL;
  *len = 0;
  do {
    if (!pathname)
      break;
    size_t pathname_len = strlen(pathname);
    if (pathname[pathname_len] == '\\' || pathname[pathname_len] == '/') {
      *path = sk__strdup(pathname);
      *len = pathname_len;
      r = SK_OK;
      break;
    }
    char *found = strrchr(pathname, '\\');
    if (found == NULL)
      found = strrchr(pathname, '/');
    if (!found)
      break;
    *len = found - pathname;
    *path = sk__strdup(pathname);
    (*path)[*len] = 0;
    r = SK_OK;
  } while (0);
  return r;
}
int sk_get_propath(char **propath, size_t *len) {
  int r = SK_NO;
  do {
    if (SK_OK != sk_get_exepath(propath, len))
      break;
    char *found = strrchr(*propath, '\\');
    if (found == NULL)
      found = strrchr(*propath, '/');
    if (!found)
      break;
    *len = found - (*propath);
    (*propath)[*len] = 0;
    r = SK_OK;
  } while (0);
  return r;
}