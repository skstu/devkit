#include <stdafx.h>

#define SK_STRINGIFY(v) SK_STRINGIFY_HELPER(v)
#define SK_STRINGIFY_HELPER(v) #v

#define SK_VERSION_STRING_BASE                                                 \
  SK_STRINGIFY(SK_VERSION_MAJOR)                                               \
  "." SK_STRINGIFY(SK_VERSION_MINOR) "." SK_STRINGIFY(SK_VERSION_PATCH)

#if SK_VERSION_IS_RELEASE
#define SK_VERSION_STRING SK_VERSION_STRING_BASE
#else
#define SK_VERSION_STRING SK_VERSION_STRING_BASE "-" SK_VERSION_SUFFIX
#endif

typedef struct {
  sk_malloc_func local_malloc;
  sk_realloc_func local_realloc;
  sk_calloc_func local_calloc;
  sk_free_func local_free;
} sk__allocator_t;

static sk__allocator_t sk__allocator = {
    malloc,
    realloc,
    calloc,
    free,
};

void *sk__malloc(size_t size) {
  void *result = NULL;
  do {
    if (size <= 0)
      break;
    result = sk__allocator.local_malloc(size);
    if (!result)
      break;
    memset(result, 0x00, size);
  } while (0);
  return result;
}
void sk__free_s(void **pptr) {
  if (*pptr) {
    sk__allocator.local_free(*pptr);
    *pptr = NULL;
  }
}
void sk__free(void *ptr) {
  int saved_errno;

  /* Skstu expects that free() does not clobber errno.  The system allocator
   * honors that assumption but custom allocators may not be so careful.
   */
  saved_errno = errno;
  sk__allocator.local_free(ptr);
  errno = saved_errno;
}

void *sk__calloc(size_t count, size_t size) {
  return sk__allocator.local_calloc(count, size);
}

void *sk__realloc(void *ptr, size_t size) {
  if (size > 0)
    return sk__allocator.local_realloc(ptr, size);
  sk__free(ptr);
  return NULL;
}

void *sk__reallocf(void *ptr, size_t size) {
  void *newptr;

  newptr = sk__realloc(ptr, size);
  if (newptr == NULL)
    if (size > 0)
      sk__free(ptr);

  return newptr;
}

char *sk__strdup(const char *s) {
  size_t len = strlen(s) + 1;
  char *m = (char *)sk__malloc(len);
  if (m == NULL)
    return NULL;
  return (char *)sk__memcpy(m, s, len);
}

void *sk__memndup(const void *src, size_t n) {
  void *m = NULL;
  do {
    if (!src || n <= 0)
      break;
    m = sk__malloc(n);
    if (!m)
      break;
    sk__memcpy(m, src, n);
  } while (0);
  return m;
}

char *sk__strndup(const char *s, size_t n) {
  char *m;
  size_t len = strlen(s);
  if (n < len)
    len = n;
  m = sk__malloc(len + 1);
  if (m == NULL)
    return NULL;
  m[len] = '\0';
  return sk__memcpy(m, s, len);
}
char *sk__strcpy(char *dest, const char *src) {
  char *r = NULL;
  do {
    if (!dest || !src)
      break;
    r = strcpy(dest, src);
  } while (0);
  return r;
}
char *sk__strncpy(char *dest, const char *src, size_t n) {
  char *r = NULL;
  do {
    if (!dest || !src || n <= 0)
      break;
    r = strncpy(dest, src, n);
  } while (0);
  return r;
}
void *sk__memcpy(void *dest, const void *src, size_t n) {
  void *r = NULL;
  do {
    if (!dest || !src || n <= 0)
      break;
    r = memcpy(dest, src, n);
  } while (0);
  return r;
}

int sk__memcmp(const char *s1, const char *s2, size_t n) {
  int r = -1;
  do {
    if (!s1 || !s2 || n <= 0)
      break;
    if (strlen(s1) < n || strlen(s2) < n)
      break;
    r = memcmp(s1, s2, n);
  } while (0);
  return r;
}
#if 0
//#include <stdio.h>
//#include <stdarg.h>
int curl_msprintf(char *buffer, const char *format, ...)
{
  va_list ap_save; /* argument pointer */
  int retcode;
  va_start(ap_save, format);
  retcode = dprintf_formatf(&buffer, storebuffer, format, ap_save);
  va_end(ap_save);
  *buffer = 0; /* we terminate this with a zero byte */
  return retcode;
}
int sk__sprintf(char *buffer, size_t buffer_size, const char *format, ...) {
  int r = 0;
  va_list args;
  va_start(args, format);
  r = vsprintf(buffer, format, args);
  va_end(args);
  return r;
}
#endif

void sk_mem_free(void **ptr) {
  sk__free(*ptr);
  *ptr = NULL;
}
void *sk_mem_malloc(size_t len) {
  return sk__malloc(len);
}

static const char *sk__unknown_err_code(int err) {
  char buf[32];
  char *copy;

  // snprintf(buf, sizeof(buf), "Unknown system error %d", err);
  sprintf(buf, "Unknown system error %d", err);
  copy = sk__strdup(buf);

  return copy != NULL ? copy : "Unknown system error";
}

#define SK_ERR_NAME_GEN(name, _)                                               \
  case SK_##name:                                                              \
    return #name;
const char *sk_err_name(int err) {
  switch (err) { SK_ERRNO_MAP(SK_ERR_NAME_GEN) }
  return sk__unknown_err_code(err);
}
#undef SK_ERR_NAME_GEN

#define SK_STRERROR_GEN(name, msg)                                             \
  case SK_##name:                                                              \
    return msg;
const char *sk_strerror(int err) {
  switch (err) { SK_ERRNO_MAP(SK_STRERROR_GEN) }
  return sk__unknown_err_code(err);
}
#undef SK_STRERROR_GEN

const char *sk_handle_type_name(sk_handle_type type) {
  switch (type) {
#define XX(uc, lc)                                                             \
  case SK_##uc:                                                                \
    return #lc;
    SK_HANDLE_TYPE_MAP(XX)
#undef XX
  case SK_FILE:
    return "file";
  case SK_HANDLE_TYPE_MAX:
  case SK_UNKNOWN_HANDLE:
    return NULL;
  }
  return NULL;
}

int sk_translate_sys_error(int sys_errno) {
  return SK_NO;
}

int sk_replace_allocator(sk_malloc_func malloc_func,
                         sk_realloc_func realloc_func,
                         sk_calloc_func calloc_func, sk_free_func free_func) {
  if (malloc_func == NULL || realloc_func == NULL || calloc_func == NULL ||
      free_func == NULL) {
    return SK_NO;
  }

  sk__allocator.local_malloc = malloc_func;
  sk__allocator.local_realloc = realloc_func;
  sk__allocator.local_calloc = calloc_func;
  sk__allocator.local_free = free_func;

  return 0;
}

unsigned int sk_version(void) {
  return SK_VERSION_HEX;
}

const char *sk_version_string(void) {
  return SK_VERSION_STRING;
}

/*
int sk__sprintf(char *buffer, size_t buffer_size, const char *format, ...) {
  int r = 0;
  va_list ap_save;
  int retcode;
  va_start(ap_save, format);
  r = sprintf(buffer, format, ap_save);
  va_end(ap_save);
  *buffer = 0;
  return r;
}*/

/* Also covers __clang__ and __INTEL_COMPILER. Disabled on Windows because
 * threads have already been forcibly terminated by the operating system
 * by the time destructors run, ergo, it's not safe to try to clean them up.
 */
#if defined(__GNUC__) && !defined(_WIN32)
__attribute__((destructor))
#endif
int
sk_library_shutdown(void)
{
  static int was_shutdown = 0;
  do {
    if (was_shutdown == 1)
      break;

    was_shutdown = 1;
  } while (0);
  return SK_OK;
}

#if defined(__GNUC__) && !defined(_WIN32)
__attribute__((constructor))
#endif
int
sk_library_startup(void)
{
  static int was_startup;
  do {
    if (was_startup == 1)
      break;

    was_startup = 1;
  } while (0);
  return SK_OK;
}