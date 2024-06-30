#include "stdafx.h"

static ULONG_PTR gdiplustoken_ = 0;
static Gdiplus::GdiplusStartupInput gdiplusStartupInput_ = {0};

Overlay::Overlay() {
  Init();
}
Overlay::~Overlay() {
  UnInit();
}
void Overlay::Release() const {
  delete this;
}
bool Overlay::Ready() const {
  return open_.load();
}
void Overlay::Init() {
  config_ = new Config();
  Gdiplus::GdiplusStartup(&gdiplustoken_, &gdiplusStartupInput_, NULL);
}
void Overlay::UnInit() {
  Gdiplus::GdiplusShutdown(gdiplustoken_);
  config_->Release();
}
IOverlay::IConfig *Overlay::ConfigGet() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  auto pConfig = dynamic_cast<IOverlay::IConfig *>(config_);
  return pConfig;
}
void Overlay::OnUioEvent(const uiohook::IUioEvent *pEvent) const {
  for (const auto &window : window_s_)
    window->OnUioEvent(pEvent);
}
bool Overlay::Screenshot(const OverlayWindowType &type) const {
  bool result = false;
  for (const auto &window : window_s_) {
    if (!(window->Type() & type))
      continue;
    result = window->Screenshot();
  }
  return result;
}
void Overlay::RegisterScreenshotFinishedCb(const tfScreenshotFinishedCb &cb) {
  std::lock_guard<std::mutex> lock{*mutex_};
  screenshot_finished_cb_ = cb;
}
void Overlay::OnScreenshotFinished(const OverlayWindowType &type,
                                   const IOverlay::IStream *data) const {
  std::lock_guard<std::mutex> lock{*mutex_};
  if (screenshot_finished_cb_)
    screenshot_finished_cb_(type, data);
}
bool Overlay::Start(const OverlayWindowType &type) {
  do {
    if (open_.load())
      break;
    if (OverlayWindowType::OVERLAY_WINDOW_CV & type) {
      window_s_.emplace_back(dynamic_cast<IWindow *>(new WindowCv()));
    }
    if (OverlayWindowType::OVERLAY_WINDOW_UI & type) {
      window_s_.emplace_back(dynamic_cast<IWindow *>(new WindowUi()));
    }
    if (OverlayWindowType::OVERLAY_WINDOW_BK & type) {
      window_s_.emplace_back(dynamic_cast<IWindow *>(new WindowBk()));
    }
    if (OverlayWindowType::OVERLAY_WINDOW_ELE & type) {
      window_s_.emplace_back(dynamic_cast<IWindow *>(new WindowEle()));
    }

    for (auto it = window_s_.begin(); it != window_s_.end(); ++it) {
      (*it)->Create();
    }
    open_.store(true);
  } while (0);
  return open_.load();
}
void Overlay::Stop() {
  do {
    if (!open_.load())
      break;
    for (auto &w : window_s_) {
      w->Destroy();
      w->Release();
    }
    window_s_.clear();
    open_.store(false);
  } while (0);
}
void Overlay::SetPosition(const long &x, const long &y, const long &cx,
                          const long &cy) const {
  for (const auto &window : window_s_) {
    window->SetPosition(x, y, cx, cy);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
Overlay *__gpOverlay = nullptr;
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpOverlay) {
      break;
    }
    __gpOverlay = new Overlay();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<IOverlay *>(__gpOverlay));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpOverlay) {
      break;
    }
    delete __gpOverlay;
    __gpOverlay = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif
