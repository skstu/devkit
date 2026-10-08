#ifndef DEVKIT_LIBFLUI_HOST_H
#define DEVKIT_LIBFLUI_HOST_H
#include "flui.h"
#if defined(_WIN32)
#if defined(LIBFLUI_CLIENT_BUILD)
#define FLUI_CLIENT_API __declspec(dllexport)
#else
#define FLUI_CLIENT_API __declspec(dllimport)
#endif
#else
#define FLUI_CLIENT_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental SDK-owned host profile (Android/iOS/Windows/Linux).
 * The consumer module exports this entry; libflui itself does not implement it.
 * Called once, after host attachment, on the SDK UI owner thread. Return a
 * flui_status. Do not run an event loop, throw, unload or restart the controller.
 * Other threads must use flui_dispatch. See docs/HOST_PREVIEW.md for capabilities
 * and process-lifetime restrictions; this is not the macOS native host profile.
 */
FLUI_CLIENT_API flui_status FLUI_CALL flui_client_start(uint32_t abi_version);
#ifdef __cplusplus
}
#endif
#endif
