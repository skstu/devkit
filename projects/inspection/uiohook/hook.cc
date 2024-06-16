#include "stdafx.h"

enum class hook_dispatch_proc_status : int {
  UNDEFINED = 0x00,
  DISABLED = 0x10,
  ENABLED = 0x20,
  FAILED = 0x30,
};
static uiohook_event *gsp_event_ = nullptr;
static hook_dispatch_proc_status gs_hook_dispatch_status =
    hook_dispatch_proc_status::UNDEFINED;
static std::shared_ptr<std::mutex> gs_mutex = std::make_shared<std::mutex>();
static std::condition_variable gs_cv_perform_thread;

static bool logger_proc(unsigned int level, const char *format, ...);

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
  gsp_event_ = (uiohook_event *)malloc(sizeof(uiohook_event));
  memset(gsp_event_, 0x00, sizeof(uiohook_event));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_ALT_L, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_ALT_R, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_CONTROL_L, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_CONTROL_R, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_ESCAPE, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_SHIFT_L, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_SHIFT_R, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_A, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_B, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_C, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_D, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_E, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_F, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_G, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_H, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_I, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_J, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_K, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_L, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_M, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_N, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_O, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_P, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_Q, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_R, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_S, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_T, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_U, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_V, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_W, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_X, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_Y, false));
  uio_virtual_key_pressed_s_.insert(
      std::make_pair(UioVirtualKeyCodes::UIO_VC_Z, false));

  uio_virtual_mouse_pressed_s_.insert(
      std::make_pair(UioVirtualMouseButtons::UIO_MOUSE_BUTTON_ANY, false));
  uio_virtual_mouse_pressed_s_.insert(
      std::make_pair(UioVirtualMouseButtons::UIO_MOUSE_BUTTON_EXTRA1, false));
  uio_virtual_mouse_pressed_s_.insert(
      std::make_pair(UioVirtualMouseButtons::UIO_MOUSE_BUTTON_EXTRA2, false));
  uio_virtual_mouse_pressed_s_.insert(
      std::make_pair(UioVirtualMouseButtons::UIO_MOUSE_BUTTON_LEFT, false));
  uio_virtual_mouse_pressed_s_.insert(
      std::make_pair(UioVirtualMouseButtons::UIO_MOUSE_BUTTON_MIDDLE, false));
  uio_virtual_mouse_pressed_s_.insert(
      std::make_pair(UioVirtualMouseButtons::UIO_MOUSE_BUTTON_RIGHT, false));
}
void Hook::UnInit() {
  free(gsp_event_);
}
void Hook::AppendEvent(const uiohook_event *ev) {
  uiohook_event _ev;
  memcpy(&_ev, ev, sizeof(uiohook_event));
  uiohook_event_q_.push(_ev);
}
void Hook::SetKeyState(const UioVirtualKeyCodes &key, const bool &pressed) {
  std::lock_guard<std::mutex> lock{*mutex_};
  do {
    auto found = uio_virtual_key_pressed_s_.find(key);
    if (found == uio_virtual_key_pressed_s_.end())
      break;
    found->second = pressed;
  } while (0);
}
void Hook::SetMouseButtonState(const UioVirtualMouseButtons &button,
                               const bool &pressed) {
  std::lock_guard<std::mutex> lock{*mutex_};
  do {
    auto found = uio_virtual_mouse_pressed_s_.find(button);
    if (found == uio_virtual_mouse_pressed_s_.end())
      break;
    found->second = pressed;
  } while (0);
}
void Hook::UnregisterEvent(const unsigned long long &key) {
  std::lock_guard<std::mutex> lock{*mutex_};
  do {
    if (!key)
      break;
    auto found = uioevent_cbs_.find(key);
    if (found == uioevent_cbs_.end())
      break;
    uioevent_cbs_.erase(found);
  } while (0);
}
void Hook::RegisterEventCb(const unsigned long long &key, const tfEventCb &cb) {
  std::lock_guard<std::mutex> lock{*mutex_};
  do {
    if (!key || !cb)
      break;
    auto found = uioevent_cbs_.find(key);
    if (found != uioevent_cbs_.end())
      uioevent_cbs_.erase(found);
    uioevent_cbs_.emplace(key, cb);
  } while (0);
}
void Hook::OnEvent(const uiohook_event &event) const {
  std::lock_guard<std::mutex> lock{*mutex_};
  for (const auto &target : uioevent_cbs_) {
    UioEvent *pEvent = new UioEvent(event);
    pEvent->SetUserData(target.first);
    pEvent->SetMousePressed(uio_virtual_mouse_pressed_s_);
    pEvent->SetKeyPressed(uio_virtual_key_pressed_s_);
    target.second(pEvent);
    pEvent->Release();
  }
}
bool Hook::Start() {
  do {
    if (open_.load())
      break;
    open_.store(true);
    threads_.emplace_back([this]() { Perform(); });
    threads_.emplace_back([this]() { Worker(); });
    std::unique_lock<std::mutex> lock{*gs_mutex};
    gs_cv_perform_thread.wait(lock, [this]() {
      bool result = true;
      switch (gs_hook_dispatch_status) {
      case hook_dispatch_proc_status::ENABLED: {
        open_.store(true);
      } break;
      case hook_dispatch_proc_status::FAILED: {

      } break;
      default:
        result = false;
        break;
      }
      return result;
    });
  } while (0);
  return open_.load();
}
void Hook::Stop() {
  do {
    if (!open_.load())
      break;
    uiohook_event esc_event;
    esc_event.type = EVENT_KEY_RELEASED;
    esc_event.data.keyboard.rawcode = 0x01;
    esc_event.data.keyboard.keycode = VC_ESCAPE;
    // hook_post_event(&esc_event);
    uiohook_event_q_.push(esc_event);
    std::unique_lock<std::mutex> lock{*gs_mutex};
    gs_cv_perform_thread.wait(lock, [this]() {
      bool result = true;
      switch (gs_hook_dispatch_status) {
      case hook_dispatch_proc_status::DISABLED: {
        open_.store(false);
      } break;
      case hook_dispatch_proc_status::FAILED: {

      } break;
      default:
        result = false;
        break;
      }
      return result;
    });

    for (auto &t : threads_)
      t.join();
    threads_.clear();
  } while (0);
}
void Hook::Worker() {
  do {
    do {
      auto evs = uiohook_event_q_.pops();
      if (evs.empty())
        break;
      for (const auto &event : evs) {
        switch (event.type) {
        case EVENT_KEY_PRESSED: {
          SetKeyState(
              static_cast<UioVirtualKeyCodes>(event.data.keyboard.keycode),
              true);
        } break;
        case EVENT_KEY_RELEASED: {
          if (VC_ESCAPE == event.data.keyboard.keycode) {
            // logger_proc(LOG_LEVEL_INFO, "%s", "uiohook module exit.");
            hook_stop();
            break;
          }
          SetKeyState(
              static_cast<UioVirtualKeyCodes>(event.data.keyboard.keycode),
              false);
        } break;
        case EVENT_MOUSE_CLICKED: {
          auto sk = 0;
        } break;
        case EVENT_MOUSE_PRESSED: {

        } break;
        case EVENT_MOUSE_RELEASED: {
          switch (event.data.mouse.button) {
          case MOUSE_BUTTON1: {
          } break;
          default:
            break;
          }
        } break;
        case EVENT_MOUSE_MOVED: {
        } break;
        case EVENT_MOUSE_DRAGGED: {
          auto v = &event.data.mouse;
          auto sk = 0;
        } break;
        case EVENT_MOUSE_WHEEL: {
        } break;
        default:
          break;
        }

        OnEvent(event);
      }
    } while (0);
    /*if (!open_.load())
      break;*/
    if (!open_.load())
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  } while (1);
}
//!@ Private
void Hook::__MouseSetPos(const long &x, const long &y,
                         const UioVirtualMouseButtons &btnType) const {
  memset(gsp_event_, 0x00, sizeof(uiohook_event));
  gsp_event_->time = stl::Time::TimeStamp<std::chrono::milliseconds>();
  gsp_event_->type = EVENT_MOUSE_MOVED;
  gsp_event_->data.mouse.button = static_cast<uint16_t>(btnType);
  gsp_event_->data.mouse.x = x;
  gsp_event_->data.mouse.y = y;
  hook_post_event(gsp_event_);
}
//!@ Private
void Hook::__MouseMoveTo(long x, long y, long step,
                         const UioVirtualMouseButtons &btnType) const {
  memset(gsp_event_, 0x00, sizeof(uiohook_event));
  gsp_event_->time = stl::Time::TimeStamp<std::chrono::milliseconds>();
  long _step = __min(step, 100);
  _step = __max(1, _step);

  POINT src = {0};
  GetCursorPos(&src);

  const long offset_x = x - src.x;
  const long offset_y = y - src.y;

  const long len =
      static_cast<long>(sqrt(pow(offset_x, 2) + pow(offset_y, 2)) * 1.0);

  if (len >= _step) {
    step -= (len % step);
  } else {
    _step = len;
  }
  // Calculate the step increment for x and y
  double increment_x = static_cast<double>(offset_x) / len;
  double increment_y = static_cast<double>(offset_y) / len;

  for (long i = 0; i < len; i += _step) {
    gsp_event_->type = EVENT_MOUSE_MOVED;
    gsp_event_->data.mouse.button = static_cast<uint16_t>(btnType);
    gsp_event_->data.mouse.x = static_cast<long>(src.x + i * increment_x);
    gsp_event_->data.mouse.y = static_cast<long>(src.y + i * increment_y);
    hook_post_event(gsp_event_);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  // Move to the final position to ensure accuracy
  __MouseSetPos(x, y, btnType);
}
void Hook::MouseWheel(const long &x, const long &y, const long &amount,
                      const long &rotation) const {
  std::lock_guard<std::mutex> lock{*mutex_};
  __MouseSetPos(x, y);
  gsp_event_->time = stl::Time::TimeStamp<std::chrono::milliseconds>();
  gsp_event_->type = EVENT_MOUSE_WHEEL;
  gsp_event_->data.mouse.button = MOUSE_NOBUTTON;
  gsp_event_->data.wheel.x = x;
  gsp_event_->data.wheel.y = y;
  gsp_event_->data.wheel.amount = amount;
  gsp_event_->data.wheel.rotation = rotation;
  hook_post_event(gsp_event_);
}
void Hook::MouseClick(const UioVirtualMouseButtons &btnType, const long &x,
                      const long &y, const unsigned long &count) const {
  std::lock_guard<std::mutex> lock{*mutex_};
  __MouseSetPos(x, y);
  gsp_event_->time = stl::Time::TimeStamp<std::chrono::milliseconds>();
  gsp_event_->data.mouse.x = x;
  gsp_event_->data.mouse.y = y;
  gsp_event_->data.mouse.button = static_cast<uint16_t>(btnType);
  gsp_event_->data.mouse.clicks = 1;
  for (unsigned long i = 0; i < count; ++i) {
    gsp_event_->type = EVENT_MOUSE_PRESSED;
    gsp_event_->time += 10;
    hook_post_event(gsp_event_);

    gsp_event_->type = EVENT_MOUSE_RELEASED;
    gsp_event_->time += 10;
    hook_post_event(gsp_event_);
  }
}
void Hook::MouseDragged(const long &to_x, const long &to_y, const long &from_x,
                        const long &from_y) const {
  std::lock_guard<std::mutex> lock{*mutex_};
  __MouseSetPos(from_x, from_y);
  gsp_event_->time = stl::Time::TimeStamp<std::chrono::milliseconds>();
  gsp_event_->data.mouse.x = from_x;
  gsp_event_->data.mouse.y = from_x;
  gsp_event_->data.mouse.button =
      static_cast<uint16_t>(UioVirtualMouseButtons::UIO_MOUSE_BUTTON_LEFT);
  gsp_event_->type = EVENT_MOUSE_PRESSED;
  hook_post_event(gsp_event_);
  __MouseMoveTo(to_x, to_y, 30, UioVirtualMouseButtons::UIO_MOUSE_BUTTON_LEFT);
  gsp_event_->time = stl::Time::TimeStamp<std::chrono::milliseconds>();
  gsp_event_->data.mouse.x = to_x;
  gsp_event_->data.mouse.y = to_y;
  gsp_event_->data.mouse.button =
      static_cast<uint16_t>(UioVirtualMouseButtons::UIO_MOUSE_BUTTON_LEFT);
  gsp_event_->type = EVENT_MOUSE_RELEASED;
  hook_post_event(gsp_event_);
}
void Hook::MouseSetTo(const long &x, const long &y) const {
  std::lock_guard<std::mutex> lock{*mutex_};
  __MouseSetPos(x, y);
}
void Hook::MouseMoveTo(long x, long y, long step) const {
  std::lock_guard<std::mutex> lock{*mutex_};
  __MouseMoveTo(x, y, step);
}

void Hook::Perform() {
  hook_set_logger_proc(&logger_proc);
  // NOTE: The following callback executes on the same thread that
  // hook_run() is called from.  This is important because hook_run()
  // attaches to the operating systems event dispatcher and may delay event
  // delivery to the target application. Furthermore, some operating systems
  // may choose to disable your hook if it takes too long to process.  If
  // you need to do any extended processing, please do so by copying the
  // event to your own queued dispatch thread. Set the event callback for
  // uiohook events.
  hook_set_dispatch_proc([](uiohook_event *const event) {
    switch (event->type) {
    case EVENT_HOOK_ENABLED: {
      std::unique_lock<std::mutex> lock{*gs_mutex, std::defer_lock};
      lock.lock();
      gs_hook_dispatch_status = hook_dispatch_proc_status::ENABLED;
      gs_cv_perform_thread.notify_one();
      // std::cout << "Hook enabled." << std::endl;
      lock.unlock();
    } break;
    case EVENT_HOOK_DISABLED: {
      std::unique_lock<std::mutex> lock{*gs_mutex, std::defer_lock};
      lock.lock();
      gs_hook_dispatch_status = hook_dispatch_proc_status::DISABLED;
      // std::cout << "Hook disabled." << std::endl;
      // logger_proc(LOG_LEVEL_INFO, "%s", "Hook disabled.");
      gs_cv_perform_thread.notify_one();
      lock.unlock();
    } break;
    default:
      __gpHook->AppendEvent(event);
      break;
    }
  });

  // Start the hook and block.
  // NOTE If EVENT_HOOK_ENABLED was delivered, the status will always
  // succeed.
  // UIOHOOK_SUCCESS
  if (UIOHOOK_SUCCESS != hook_run()) {
    std::unique_lock<std::mutex> lock{*gs_mutex, std::defer_lock};
    lock.lock();
    gs_cv_perform_thread.notify_one();
    lock.unlock();
  }
}

bool logger_proc(unsigned int level, const char *format, ...) {
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
void Hook::Free(void **p) const {
  do {
    if (!*p)
      break;
    free(*p);
    *p = nullptr;
  } while (0);
}
bool Hook::SetClipboardText(const char *text, const long &len) const {
  bool result = false;
  do {
    if (!text || len <= 0)
      break;
    HGLOBAL hGlobal = nullptr;
    if (FALSE == OpenClipboard(nullptr))
      break;
    do {
      if (FALSE == EmptyClipboard())
        break;
      hGlobal = GlobalAlloc(GMEM_MOVEABLE, len);
      if (!hGlobal)
        break;
      char *pGlobal = static_cast<char *>(GlobalLock(hGlobal));
      if (!pGlobal)
        break;
      memcpy(pGlobal, text, len);
      GlobalUnlock(hGlobal);
      if (!SetClipboardData(CF_TEXT, hGlobal))
        break;
      result = true;
    } while (0);
    if (!result && hGlobal) {
      GlobalFree(hGlobal);
      hGlobal = nullptr;
    }
    CloseClipboard();
  } while (0);
  return result;
}
bool Hook::GetClipboardText(char **text, long *len) const {
  *text = nullptr;
  *len = 0;
  bool result = false;
  do {
    HANDLE hData = nullptr;
    if (FALSE == OpenClipboard(nullptr))
      break;
    do {
      hData = GetClipboardData(CF_TEXT);
      if (!hData)
        break;
      SIZE_T dataSize = GlobalSize(hData);
      char *pszText = static_cast<char *>(GlobalLock(hData));
      if (!pszText || dataSize <= 0)
        break;
      *text = (char *)malloc(dataSize);
      *len = dataSize;
      memcpy(*text, pszText, *len);
      result = true;
    } while (0);
    if (hData)
      GlobalUnlock(hData);
    CloseClipboard();
  } while (0);
  return result;
}
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
Hook *__gpHook = nullptr;
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