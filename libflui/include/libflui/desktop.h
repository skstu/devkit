#ifndef DEVKIT_LIBFLUI_DESKTOP_H
#define DEVKIT_LIBFLUI_DESKTOP_H
#include "flui.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Retained documents are generic controls, never business commands. IDs are
 * decimal strings. A tree replaces the document; patches replace attributes on
 * existing IDs atomically. The same queue/input limits as set_state apply. */
FLUI_API flui_status FLUI_CALL flui_window_set_tree(flui_window,
                                                    flui_string json,
                                                    uint64_t request);
FLUI_API flui_status FLUI_CALL flui_window_patch(flui_window, flui_string json,
                                                 uint64_t request);
#define FLUI_EVENT_CLOSE_REQUEST 7u
/* title, size (w,h), minimum (w,h), managed_close (true/false). */
FLUI_API flui_status FLUI_CALL flui_window_property(flui_window,
                                                    flui_string key,
                                                    flui_string value);
FLUI_API flui_status FLUI_CALL flui_window_request_close(flui_window);
FLUI_API flui_status FLUI_CALL flui_window_close(flui_window);
/* UI thread, outside an event callback. A modal dialog has its own engine;
 * other UI timers and renderer messages continue. Close ends this loop. */
FLUI_API flui_status FLUI_CALL flui_window_run_modal(flui_window);
typedef void(FLUI_CALL *flui_callback)(void *user);
/* Thread safe; executes once on the UI thread, including when its former
 * window has closed. User owns context until callback; use weak ownership. */
FLUI_API flui_status FLUI_CALL flui_dispatch(flui_callback, void *user);
typedef uint64_t flui_timer;
FLUI_API flui_status FLUI_CALL flui_timer_create(uint32_t interval_ms,
                                                 flui_callback, void *user,
                                                 flui_timer *);
FLUI_API flui_status FLUI_CALL flui_timer_destroy(flui_timer);
/* Synchronous borrowed UTF-8 result; invoked at most once before return. */
typedef void(FLUI_CALL *flui_text_callback)(flui_string, void *user);
#define FLUI_FILE_OPEN 1u
#define FLUI_FILE_SAVE 2u
#define FLUI_FILE_DIRECTORY 3u
FLUI_API flui_status FLUI_CALL flui_file_dialog(uint32_t kind,
                                                flui_string title,
                                                flui_string initial_name,
                                                flui_string extensions,
                                                flui_text_callback, void *user);
/* Additive asynchronous picker. UI thread; accepts OPEN or DIRECTORY.
 * FLUI_OK transfers callback ownership until exactly one completion on the UI
 * thread. A successful cancellation returns FLUI_OK with an empty path.
 * Nonzero immediate status means no callback. Result bytes are borrowed only
 * during the callback; callers must retain a weak owner if their window closes.
 * Mobile providers may return an app-cache copy; paths are not durable grants.
 * Only one picker may be outstanding. Existing synchronous ABI is unchanged. */
typedef void(FLUI_CALL *flui_file_callback)(flui_status, flui_string path, void *user);
FLUI_API flui_status FLUI_CALL flui_file_dialog_async(uint32_t kind,
    flui_string title, flui_file_callback, void *user);
FLUI_API flui_status FLUI_CALL flui_executable_path(flui_text_callback,
                                                    void *user);
/* Atomic binary data write, at most 64 MiB; path is UTF-8. */
FLUI_API flui_status FLUI_CALL flui_write_file_atomic(flui_string path,
                                                      flui_string content);
FLUI_API flui_status FLUI_CALL flui_bell(void);
/* XML SAX visitor: 1=open(tag), 2=attribute(name,value), 3=close(tag).
 * Bounded UTF-8 XML only, external entities/DTD rejected. Visitor returning
 * nonzero aborts. Visitors must not throw across the C ABI. */
typedef int32_t(FLUI_CALL *flui_xml_callback)(uint32_t, flui_string name,
                                              flui_string value, void *user);
FLUI_API flui_status FLUI_CALL flui_xml_visit(flui_string, flui_xml_callback,
                                              void *user);
#ifdef __cplusplus
}
#endif
#endif
