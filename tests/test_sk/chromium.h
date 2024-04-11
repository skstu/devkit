#ifndef BASE_SK_FPS_H_
#define BASE_SK_FPS_H_

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "base/base_export.h"
#include "base/containers/span.h"
#include "base/strings/string_piece.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {
namespace chromium {
namespace ipc {}        // namespace ipc
namespace network {}    // namespace network
namespace extensions {} // namespace extensions
namespace fps {}        // namespace fps
} // namespace chromium
namespace sk {
enum SKRES {
  SK_OK = 0,
  SK_NO = 1,
};
enum IPC_MSGTYPE {
  IPC_MSGTYPE_RENDER_PROCESS = 0,
  IPC_MSGTYPE_RENDER_FRAME = 1,

  IPC_MSGTYPE_BEGIN = IPC_MSGTYPE_RENDER_PROCESS,
  IPC_MSGTYPE_END = IPC_MSGTYPE_RENDER_FRAME,
};
struct IPC_MSG {
  unsigned long head;
  int type;
  size_t len;
  unsigned long tail;
  char data[0];
};
BASE_EXPORT void sk_on_chromium_startup();
BASE_EXPORT void sk_on_chromium_shutdown();
typedef void (*load_chromium_extensions_cb)(const char *, const wchar_t *,
                                            void *);
BASE_EXPORT void
sk_on_chromium_extensions_installer(load_chromium_extensions_cb, void *);
BASE_EXPORT int
sk_set_google_apit_key(const char *GOOGLE_API_KEY,
                       const char *GOOGLE_DEFAULT_CLIENT_ID,
                       const char *GOOGLE_DEFAULT_CLIENT_SECRET);
BASE_EXPORT int sk_base64encode(const char *src, size_t srcLen, char **dst,
                                size_t *dstLen);
BASE_EXPORT int sk_base64decode(const char *src, size_t srcLen, char **dst,
                                size_t *dstLen);
BASE_EXPORT int sk_get_current_process_path(char **, size_t *);
BASE_EXPORT int sk_get_temp_path(char **, size_t *);
BASE_EXPORT int sk_write_file(const char *file_path, const char *src,
                              size_t srcLen);
BASE_EXPORT int sk_write_file_addto(const char *file_path, const char *src,
                                    size_t srcLen);
BASE_EXPORT int sk_read_file(const char *file_path, char **dst, size_t *dstLen);
BASE_EXPORT void sk_free_s(void **ptr);
BASE_EXPORT void *sk_malloc(size_t size);
BASE_EXPORT void *sk_realloc(void *ptr, size_t size);
namespace fps {
typedef void *CONFIG_HANDLE;
BASE_EXPORT int sk_verify_configure(const char *src, size_t srcLen, char **dst,
                                    size_t *dstLen);
BASE_EXPORT size_t sk_get_default_injector_script_size();
BASE_EXPORT void sk_get_default_injector_script(char **dst, size_t *dstLen);
BASE_EXPORT const char *sk_get_default_injector_script_encode();
BASE_EXPORT CONFIG_HANDLE sk_configure_create(const char *, size_t);
BASE_EXPORT void sk_configure_destroy(CONFIG_HANDLE *);
BASE_EXPORT int sk_configure_injector_settings(CONFIG_HANDLE, char **,
                                               size_t *);
BASE_EXPORT int sk_configure_injector_script_generate(CONFIG_HANDLE, char **,
                                                      size_t *);
} // namespace fps

BASE_EXPORT int sk_ipc_message_create(int msgType, const char *src,
                                      size_t srcLen, char **dst,
                                      size_t *dstLen);
BASE_EXPORT int sk_ipc_message_parser(const char *data, size_t dataLen,
                                      IPC_MSG **msg);
BASE_EXPORT void sk_ipc_message_destroy(IPC_MSG **);

namespace conversions {
BASE_EXPORT int wide_to_ascii(const wchar_t *, size_t, char **, size_t *);
BASE_EXPORT int ascii_to_wide(const char *, size_t, wchar_t **, size_t *);
BASE_EXPORT int utf8_to_wide(const char *, size_t, wchar_t **, size_t *);
BASE_EXPORT int wide_to_utf8(const wchar_t *, size_t, char **, size_t *);
BASE_EXPORT int utf8_to_ascii(const char *, size_t, char **, size_t *);
BASE_EXPORT int ascii_to_utf8(const char *, size_t, char **, size_t *);
} // namespace conversions

} // namespace sk

} // namespace base

#endif // BASE_SK_FPS_H_