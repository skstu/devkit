#ifndef DEVKIT_LIBFLUI_H
#define DEVKIT_LIBFLUI_H
#include <stdint.h>
#if defined(_WIN32)
#if defined(LIBFLUI_BUILD)
#define FLUI_API __declspec(dllexport)
#else
#define FLUI_API __declspec(dllimport)
#endif
#define FLUI_CALL __cdecl
#else
#define FLUI_API __attribute__((visibility("default")))
#define FLUI_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define FLUI_ABI_VERSION 1u
typedef uint64_t flui_window;
typedef int32_t flui_status;
#define FLUI_OK 0
#define FLUI_INVALID_ARGUMENT 1
#define FLUI_ABI_MISMATCH 2
#define FLUI_WRONG_THREAD 3
#define FLUI_NOT_READY 4
#define FLUI_INVALID_HANDLE 5
#define FLUI_BUSY 6
#define FLUI_RUNTIME_ERROR 7
#define FLUI_DOCUMENT_ERROR 8
#define FLUI_LIMIT_EXCEEDED 9
#define FLUI_EVENT_READY 1u
#define FLUI_EVENT_ACTION 2u
#define FLUI_EVENT_COMPLETE 3u
#define FLUI_EVENT_CLOSED 4u
#define FLUI_EVENT_ERROR 5u
#define FLUI_EVENT_TICK 6u
/* UTF-8 byte spans; not necessarily NUL terminated. Borrowed only during
 * callback. */
typedef struct flui_string {
  const char *data;
  uint64_t size;
} flui_string;
typedef struct flui_event {
  uint32_t struct_size;
  uint32_t kind;
  flui_window window;
  uint64_t request_id;
  flui_status status;
  uint32_t reserved;
  flui_string name;
  flui_string value;
} flui_event;
typedef void(FLUI_CALL *flui_event_callback)(const flui_event *event,
                                             void *user);
typedef struct flui_window_options {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t width;
  uint32_t height;
  flui_string title;
  flui_event_callback on_event;
  void *user;
} flui_window_options;
/* ABI v1: all calls and callbacks on the OS main thread, except abi_version().
 * Create starts a hidden window; READY arrives through the host's event loop.
 * Strings are copied before return. No allocation must be freed across the ABI.
 * Callback may submit commands or quit; destroy/run from callback returns BUSY.
 * Callbacks must not throw. Native hosts may use their existing event loop.
 * v1 validated on macOS arm64. One renderer engine per window, at most 8
 * windows.
 */
FLUI_API uint32_t FLUI_CALL flui_abi_version(void);
FLUI_API flui_status FLUI_CALL flui_window_create(const flui_window_options *,
                                                  flui_window *out);
FLUI_API flui_status FLUI_CALL flui_window_show(flui_window);
/* Requests require READY. OK means accepted, COMPLETE reports
 * validation/application (not GPU presentation). Invalid documents/state leave
 * the last valid state intact. Maximum UTF-8 input 1 MiB, 16 pending requests /
 * 2 MiB per window. Caller handles BUSY; state updates should coalesce to the
 * latest snapshot, actions must not be replayed.
 */
FLUI_API flui_status FLUI_CALL flui_window_load_xml(flui_window,
                                                    flui_string xml,
                                                    uint64_t request_id);
FLUI_API flui_status FLUI_CALL flui_window_set_state(flui_window,
                                                     flui_string json,
                                                     uint64_t request_id);
/* Full JSON view-state snapshot, never a business command. Decimal values/IDs
 * as strings. */
/* Optional UI-thread timer: 0 stops; 100..60000 ms. Closed windows stop
 * ticking. */
FLUI_API flui_status FLUI_CALL flui_window_set_tick(flui_window,
                                                    uint32_t milliseconds);
FLUI_API flui_status FLUI_CALL flui_window_destroy(flui_window);
/* Optional standalone event loop, blocks without polling. Quit does not
 * terminate the process. Destroy remaining windows after run returns. Not for
 * nested use. */
FLUI_API flui_status FLUI_CALL flui_app_run(void);
FLUI_API flui_status FLUI_CALL flui_app_quit(void);
#ifdef __cplusplus
}
#endif
#endif
