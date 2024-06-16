#ifndef __SK_H_
#define __SK_H_

#ifdef __cplusplus
extern "C" {
#endif
/* Referenced C standard library header files */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Interface definition */
#if defined(BUILDING_SK_SHARED) && defined(USING_SK_SHARED)
#error "Define either BUILDING_SK_SHARED or USING_SK_SHARED, not both."
#endif

#ifndef SK_EXTERN
#ifdef _WIN32
/* Windows - set up dll import/export decorators. */
#if defined(BUILDING_SK_SHARED)
/* Building shared library. */
#define SK_EXTERN __declspec(dllexport)
#elif defined(USING_SK_SHARED)
/* Using shared library. */
#define SK_EXTERN __declspec(dllimport)
#else
/* Building static library. */
#define SK_EXTERN /* nothing */
#endif
#elif __GNUC__ >= 4
#define SK_EXTERN __attribute__((visibility("default")))
#elif defined(__SUNPRO_C) && (__SUNPRO_C >= 0x550) /* Sun Studio >= 8 */
#define SK_EXTERN __global
#else
#define SK_EXTERN /* nothing */
#endif
#endif /* SK_EXTERN */

#define SK__OK (-0)
#define SK__NO (-10000)
#define SK__EUNKN (-20000)
#define SK__EOF (-65533)

#define SK_ERRNO_MAP(XX)                                                       \
  XX(OK, "done")                                                               \
  XX(NO, "undone")                                                             \
  XX(EUNKN, "unknown error")

#define SK_HANDLE_TYPE_MAP(XX)                                                 \
  XX(HANDLE, handle)                                                           \
  XX(PROCESS, process)                                                         \
  XX(MUTEX, timer)                                                             \
  XX(SIGNAL, signal)

typedef enum {
#define XX(code, _) SK_##code = SK__##code,
  SK_ERRNO_MAP(XX)
#undef XX
      SK_ERRNO_MAX = SK__EOF - 1
} sk_errno_t;

typedef enum {
  SK_UNKNOWN_HANDLE = 0,
#define XX(uc, lc) SK_##uc,
  SK_HANDLE_TYPE_MAP(XX)
#undef XX
      SK_FILE,
  SK_HANDLE_TYPE_MAX
} sk_handle_type;

typedef enum {
  SK_RUN_DEFAULT = 0,
  SK_RUN_ONCE,
  SK_RUN_NOWAIT,
} sk_run_mode;

typedef enum {
  SK_F_STATUS_UNLCK = 0,
  SK_F_STATUS_WRLCK = 1,
  SK_F_STATUS_RDLCK = 2,
} sk_flock_status;

typedef enum {
  ICU_CONV_TYPE_UNKNOWN = 0,
  ICU_CONV_TYPE_UTF8,
  ICU_CONV_TYPE_UTF16,
  ICU_CONV_TYPE_UTF16LE,
  ICU_CONV_TYPE_UTF16BE,
  ICU_CONV_TYPE_UTF32,
  ICU_CONV_TYPE_UTF32LE,
  ICU_CONV_TYPE_UTF32BE,
  ICU_CONV_TYPE_ASCII,
  ICU_CONV_TYPE_GBK,
  ICU_CONV_TYPE_GB18030,
} sk_icu_conv_type;

typedef struct sk_tlock_s sk_tlock_t;
typedef struct sk_flock_s sk_flock_t;
typedef struct sk_file_s sk_file_t;
typedef struct sk_thread_s sk_thread_t;
typedef struct sk_loop_s sk_loop_t;
typedef struct sk_handle_s sk_handle_t;

typedef void (*sk_close_cb)(sk_handle_t *handle);

typedef void *(*sk_malloc_func)(size_t size);
typedef void *(*sk_realloc_func)(void *ptr, size_t size);
typedef void *(*sk_calloc_func)(size_t count, size_t size);
typedef void (*sk_free_func)(void *ptr);

SK_EXTERN int sk_library_startup(void);
SK_EXTERN int sk_library_shutdown(void);
SK_EXTERN void sk_mem_free(void **ptr);
SK_EXTERN void *sk_mem_malloc(size_t len);
SK_EXTERN int sk_translate_sys_error(int sys_errno);
SK_EXTERN const char *sk_err_name(int err);
SK_EXTERN const char *sk_strerror(int err);
SK_EXTERN unsigned int sk_version(void);
SK_EXTERN const char *sk_version_string(void);
SK_EXTERN int sk_get_pid(long *pid);
SK_EXTERN int sk_get_propath(char **path, size_t *len);
SK_EXTERN int sk_get_exepath(char **path, size_t *len);
SK_EXTERN int sk_pathname_to_path(const char *pathname, char **path,
                                  size_t *len);
SK_EXTERN int sk_replace_allocator(sk_malloc_func malloc_func,
                                   sk_realloc_func realloc_func,
                                   sk_calloc_func calloc_func,
                                   sk_free_func free_func);
SK_EXTERN const char *sk_handle_type_name(sk_handle_type type);
SK_EXTERN int sk_run(sk_loop_t *, sk_run_mode);
SK_EXTERN void sk_stop(sk_loop_t *);

SK_EXTERN sk_tlock_t *sk_tlock_create(void);
SK_EXTERN int sk_tlock_destroy(sk_tlock_t **ph);
SK_EXTERN int sk_tlock_lock(sk_tlock_t *h);
SK_EXTERN int sk_tlock_trylock(sk_tlock_t *h);
SK_EXTERN int sk_tlock_unlock(sk_tlock_t *h);

SK_EXTERN sk_flock_t *sk_fwlock_create(const char *fpath);
SK_EXTERN int sk_fwlock_destroy(sk_flock_t **ph);
SK_EXTERN int sk_fwlock_lock(sk_flock_t *h, size_t offset, size_t len);
SK_EXTERN int sk_fwlock_trylock(sk_flock_t *h, size_t offset, size_t len);
SK_EXTERN int sk_fwlock_unlock(sk_flock_t *h, size_t offset, size_t len);
SK_EXTERN void *sk_fwlock_get_file(sk_flock_t *h);

SK_EXTERN sk_file_t *sk_file_open(const char *fpath, const char *mode);
SK_EXTERN int sk_file_close(sk_file_t **ph);
SK_EXTERN int sk_file_read(sk_file_t *h, char **buf, size_t *len);
SK_EXTERN int sk_file_write(sk_file_t *h, const char *buf, size_t len);

SK_EXTERN int sk_comp_zip(const char *src, size_t srcLen, char **dst,
                          size_t *dstLen);
SK_EXTERN int sk_comp_unzip(const char *src, size_t srcLen, char **dst,
                            size_t *dstLen);
SK_EXTERN int sk_comp_gzip(const char *src, size_t srcLen, char **dst,
                           size_t *dstLen);
SK_EXTERN int sk_comp_ungzip(const char *src, size_t srcLen, char **dst,
                             size_t *dstLen);
SK_EXTERN int sk_comp_zstd(const char *src, size_t srcLen, char **dst,
                           size_t *dstLen);
SK_EXTERN int sk_comp_unzstd(const char *src, size_t srcLen, char **dst,
                             size_t *dstLen);

SK_EXTERN int sk_icu_convert(const char *src, size_t srcLen,
                             const char *toEncode, char **dst, size_t *dstLen);
SK_EXTERN int sk_icu_detect_name(const char *textIn, size_t len, char **name);
SK_EXTERN int sk_icu_detect_type(const char *textIn, size_t len,
                                 sk_icu_conv_type *type);
SK_EXTERN int sk_icu_match_type_to_name(sk_icu_conv_type type, char **name,
                                        size_t *nameLen);

SK_EXTERN int sk_ossl_md5(const char *src, size_t srcLen, char **dst,
                          size_t *dstLen);
SK_EXTERN int sk_ossl_base64_encode(const char *src, size_t srcLen, char **dst,
                                    size_t *dstLen);
SK_EXTERN int sk_ossl_base64_decode(const char *src, size_t srcLen, char **dst,
                                    size_t *dstLen);

SK_EXTERN int sk_spawn(const char *file, const char **args, size_t argc,
                       int wait);

struct sk_tlock_s {
  void *data;
  void *t_handle;
};
struct sk_flock_s {
  void *data;
  sk_file_t *flk_file;
  sk_flock_status flk_status;
};
struct sk_file_s {
  void *data;
  char *f_path;
  void *f_file;
  void *f_handle;
};
struct sk_thread_s {
  void *data;
};
struct sk_loop_s {
  /* User data - use this for whatever. */
  void *data;
};
struct sk_handle_s {
  void *data;
  sk_loop_t *loop;
  sk_handle_type type;
  sk_close_cb close_cb;
};

// @ The module architecture of this project adopts the [libuv] project
// @ See => https://github.com/libuv/libuv/
//
// @ https://skstu.com/
// @ https://github.com/skstu/
// @ https://github.com/memade/
// @ © 2024 SKSTU, Inc.
// @ 'Martell'
#ifdef __cplusplus
}
#endif
#endif ///__SK_H_