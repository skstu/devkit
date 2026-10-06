#include <libsys.hpp>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace {

bool RaiseAccessibleWindows(pid_t pid) {
  // Accessibility is an optional strengthening path. Never request trust here:
  // repeated open must not display a macOS permission prompt.
  if (!AXIsProcessTrusted())
    return false;

  AXUIElementRef application = AXUIElementCreateApplication(pid);
  if (!application)
    return false;

  bool requested =
      AXUIElementSetAttributeValue(application, kAXFrontmostAttribute,
                                   kCFBooleanTrue) == kAXErrorSuccess;
  CFTypeRef windows_value = nullptr;
  if (AXUIElementCopyAttributeValue(application, kAXWindowsAttribute,
                                    &windows_value) == kAXErrorSuccess &&
      windows_value && CFGetTypeID(windows_value) == CFArrayGetTypeID()) {
    const auto windows = static_cast<CFArrayRef>(windows_value);
    const CFIndex count = CFArrayGetCount(windows);
    for (CFIndex index = 0; index < count; ++index) {
      const auto window = reinterpret_cast<AXUIElementRef>(
          const_cast<void *>(CFArrayGetValueAtIndex(windows, index)));
      if (!window || CFGetTypeID(window) != AXUIElementGetTypeID())
        continue;
      if (AXUIElementSetAttributeValue(window, kAXMinimizedAttribute,
                                       kCFBooleanFalse) == kAXErrorSuccess) {
        requested = true;
      }
      if (AXUIElementPerformAction(window, kAXRaiseAction) == kAXErrorSuccess)
        requested = true;
    }
  }
  if (windows_value)
    CFRelease(windows_value);
  CFRelease(application);
  return requested;
}

} // namespace

bool ISystem::ActivateProcess(system_process_id_t pid) {
  if (pid <= 0)
    return false;

  bool carbon_requested = false;
  bool appkit_requested = false;
  bool accessibility_requested = false;

  @autoreleasepool {
    const pid_t native_pid = static_cast<pid_t>(pid);

    // This is the original PID-specific path that worked for BroSDK Chromium
    // instances before the lifecycle migration. Keep it synchronous: hosts
    // embedding the SDK are not required to pump the Cocoa main queue while a
    // libuv request is executing.
    NSRunningApplication *application =
        [NSRunningApplication runningApplicationWithProcessIdentifier:native_pid];
    if (application && ![application isTerminated]) {
      if ([application isHidden])
        [application unhide];
      const NSApplicationActivationOptions options =
          NSApplicationActivateAllWindows |
          NSApplicationActivateIgnoringOtherApps;
      appkit_requested = [application activateWithOptions:options] == YES;
    }

    // Carbon remains a useful immediate fallback for executables started by
    // posix_spawn/uv_spawn instead of LaunchServices. Use SetFrontProcess as
    // well as the front-window-only variant so minimized/background Chromium
    // instances are not silently accepted without a stronger request.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    ProcessSerialNumber process = {0, kNoProcess};
    if (GetProcessForPID(native_pid, &process) == noErr) {
      const OSStatus front_window = SetFrontProcessWithOptions(
          &process, kSetFrontProcessFrontWindowOnly);
      const OSStatus front_process = SetFrontProcess(&process);
      carbon_requested = front_window == noErr || front_process == noErr;
    }
#pragma clang diagnostic pop

    accessibility_requested = RaiseAccessibleWindows(native_pid);
  }

  return carbon_requested || appkit_requested || accessibility_requested;
}
