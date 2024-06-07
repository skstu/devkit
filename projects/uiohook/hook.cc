#include "stdafx.h"

static bool logger_proc(unsigned int level, const char *format, ...) {
  bool status = false;

  va_list args;
  switch (level) {
  case LOG_LEVEL_INFO:
    va_start(args, format);
    status = vfprintf(stdout, format, args) >= 0;
    va_end(args);
    break;

  case LOG_LEVEL_WARN:
  case LOG_LEVEL_ERROR:
    va_start(args, format);
    status = vfprintf(stderr, format, args) >= 0;
    va_end(args);
    break;
  }

  return status;
}

Hook::Hook() {
  Init();
}
Hook::~Hook() {
  UnInit();
}
void Hook::Release() const {
  delete this;
}
bool Hook::Ready() const {
  return false;
}
void Hook::Init() {
}
void Hook::UnInit() {
}
void Hook::AppendEvent(const uiohook_event *ev) {
  uiohook_event _ev;
  memcpy(&_ev, &ev, sizeof(uiohook_event));
  uiohook_event_q_.push(_ev);
}
bool Hook::GetControlState() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return processed_control_.load();
}
void Hook::SetControlState(const bool &processed) {
  std::lock_guard<std::mutex> lock{*mutex_};
  processed_control_.store(processed);
}
void Hook::OnMouseMove(const long &x, const long &y) const {
  std::lock_guard<std::mutex> lock{*mutex_};
  if (mouse_move_cb_)
    mouse_move_cb_(x, y);
}
void Hook::OnCaptureFinish(const long &x, const long &y, bool &exit) const {
  std::lock_guard<std::mutex> lock{*mutex_};
  if (capture_finish_cb_)
    capture_finish_cb_(x, y, exit);
}
void Hook::RegisterMouseMoveCb(const tfMouseMoveCb &cb) {
  std::lock_guard<std::mutex> lock{*mutex_};
  mouse_move_cb_ = cb;
}
void Hook::RegisterCaptureFinishCb(const tfCaptureFinishCb &cb) {
  std::lock_guard<std::mutex> lock{*mutex_};
  capture_finish_cb_ = cb;
}
bool Hook::Start() {
  do {
    if (open_.load())
      break;
    open_.store(true);
    threads_.emplace_back([this]() { Perform(); });
    threads_.emplace_back([this]() { EventNotify(); });
  } while (0);
  return open_.load();
}
void Hook::Stop() {
  do {
    if (!open_.load())
      break;

    uiohook_event esc_press;
    esc_press.type = EVENT_KEY_PRESSED;
    esc_press.data.keyboard.keycode = VC_ESCAPE;
    esc_press.data.keyboard.rawcode = 0x01;
    hook_post_event(&esc_press);

    uiohook_event esc_release;
    esc_release.type = EVENT_KEY_RELEASED;
    esc_release.data.keyboard.keycode = VC_ESCAPE;
    esc_release.data.keyboard.rawcode = 0x01;
    hook_post_event(&esc_release);

    open_.store(false);
    for (auto &t : threads_)
      t.join();
    threads_.clear();
  } while (0);
}
IUioevent *Hook::CreateEvent() {
  IUioevent *result = nullptr;
  do {

  } while (0);
  return result;
}
void Hook::EventNotify() {
  do {
    do {
      auto evs = uiohook_event_q_.pops();
      if (evs.empty())
        break;
      std::cout << evs.size() << std::endl;
      for (const auto &event : evs) {
#if 0
        switch (event.type) {
        case EVENT_HOOK_ENABLED: {
          std::cout << "Hook enabled." << std::endl;
        } break;
        case EVENT_HOOK_DISABLED: {
          std::cout << "Hook disabled." << std::endl;
        } break;
        case EVENT_KEY_TYPED: {

        } break;
        case EVENT_KEY_PRESSED: {
          switch (event.data.keyboard.keycode) {
          case VC_CONTROL_L:
          case VC_CONTROL_R: {
            if (!__gpHook->GetControlState())
              __gpHook->SetControlState(true);
          } break;
          default:
            break;
          }
        } break;
        case EVENT_KEY_RELEASED: {
          switch (event.data.keyboard.keycode) {
          case VC_ESCAPE: {
            std::cout << "UIOHook module exit." << std::endl;
            hook_stop();
          } break;
          case VC_CONTROL_L:
          case VC_CONTROL_R: {
            if (__gpHook->GetControlState())
              __gpHook->SetControlState(false);
          } break;
          default:
            break;
          }
        } break;
        case EVENT_MOUSE_CLICKED: {

        } break;
        case EVENT_MOUSE_PRESSED: {

        } break;
        case EVENT_MOUSE_RELEASED: {
          switch (event.data.mouse.button) {
          case MOUSE_BUTTON1: {
            if (!__gpHook->GetControlState())
              break;
            bool is_exit = false;
            __gpHook->OnCaptureFinish(event.data.mouse.x, event.data.mouse.y,
                                      is_exit);
            if (is_exit)
              hook_stop();
            std::cout << "capture . . ." << std::endl;
          } break;
          default:
            break;
          }
        } break;
        case EVENT_MOUSE_MOVED: {
          __gpHook->OnMouseMove(event.data.mouse.x, event.data.mouse.y);
        } break;
        case EVENT_MOUSE_DRAGGED: {

        } break;
        case EVENT_MOUSE_WHEEL: {

        } break;
        default:
          break;
        }
#endif
      }
    } while (0);
    if (!open_.load())
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  } while (1);
}
void Hook::Perform() {
  // Set the logger callback for library output.
  hook_set_logger_proc(&logger_proc);

  // NOTE: The following callback executes on the same thread that hook_run() is
  // called from.  This is important because hook_run() attaches to the
  // operating systems event dispatcher and may delay event delivery to the
  // target application. Furthermore, some operating systems may choose to
  // disable your hook if it takes too long to process.  If you need to do any
  // extended processing, please do so by copying the event to your own queued
  // dispatch thread. Set the event callback for uiohook events.
  hook_set_dispatch_proc(
      [](uiohook_event *const event) { __gpHook->AppendEvent(event); });

  // Start the hook and block.
  // NOTE If EVENT_HOOK_ENABLED was delivered, the status will always succeed.
  int status = hook_run();
  switch (status) {
  case UIOHOOK_SUCCESS:
    // Everything is ok.
    break;

  // System level errors.
  case UIOHOOK_ERROR_OUT_OF_MEMORY:
    logger_proc(LOG_LEVEL_ERROR, "Failed to allocate memory. (%#X)", status);
    break;

  // X11 specific errors.
  case UIOHOOK_ERROR_X_OPEN_DISPLAY:
    logger_proc(LOG_LEVEL_ERROR, "Failed to open X11 display. (%#X)", status);
    break;

  case UIOHOOK_ERROR_X_RECORD_NOT_FOUND:
    logger_proc(LOG_LEVEL_ERROR, "Unable to locate XRecord extension. (%#X)",
                status);
    break;

  case UIOHOOK_ERROR_X_RECORD_ALLOC_RANGE:
    logger_proc(LOG_LEVEL_ERROR, "Unable to allocate XRecord range. (%#X)",
                status);
    break;

  case UIOHOOK_ERROR_X_RECORD_CREATE_CONTEXT:
    logger_proc(LOG_LEVEL_ERROR, "Unable to allocate XRecord context. (%#X)",
                status);
    break;

  case UIOHOOK_ERROR_X_RECORD_ENABLE_CONTEXT:
    logger_proc(LOG_LEVEL_ERROR, "Failed to enable XRecord context. (%#X)",
                status);
    break;

  // Windows specific errors.
  case UIOHOOK_ERROR_SET_WINDOWS_HOOK_EX:
    logger_proc(LOG_LEVEL_ERROR,
                "Failed to register low level windows hook. (%#X)", status);
    break;

  // Darwin specific errors.
  case UIOHOOK_ERROR_AXAPI_DISABLED:
    logger_proc(LOG_LEVEL_ERROR,
                "Failed to enable access for assistive devices. (%#X)", status);
    break;

  case UIOHOOK_ERROR_CREATE_EVENT_PORT:
    logger_proc(LOG_LEVEL_ERROR, "Failed to create apple event port. (%#X)",
                status);
    break;

  case UIOHOOK_ERROR_CREATE_RUN_LOOP_SOURCE:
    logger_proc(LOG_LEVEL_ERROR,
                "Failed to create apple run loop source. (%#X)", status);
    break;

  case UIOHOOK_ERROR_GET_RUNLOOP:
    logger_proc(LOG_LEVEL_ERROR, "Failed to acquire apple run loop. (%#X)",
                status);
    break;

  case UIOHOOK_ERROR_CREATE_OBSERVER:
    logger_proc(LOG_LEVEL_ERROR,
                "Failed to create apple run loop observer. (%#X)", status);
    break;

  // Default error.
  case UIOHOOK_FAILURE:
  default:
    logger_proc(LOG_LEVEL_ERROR, "An unknown hook error occurred. (%#X)",
                status);
    break;
  }
}
Hook *__gpHook = nullptr;
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpHook) {
      break;
    }
    __gpHook = new Hook();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<IUiohook *>(__gpHook));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpHook) {
      break;
    }
    delete __gpHook;
    __gpHook = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif