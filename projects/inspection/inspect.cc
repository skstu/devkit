#include "stdafx.h"

Inspect::Inspect() {
  Init();
}
Inspect::~Inspect() {
  UnInit();
}
void Inspect::Release() const {
  delete this;
}
bool Inspect::Ready() const {
  return false;
}
void Inspect::Init() {
  do {
    if (ready_.load())
      break;
    pOverlay_ = win::IOverlay::Create("winoverlay.dll");
    pUiohook_ = uiohook::IUiohook::Create("uiohook.dll");
    pAutomation_ = win::IAutomation::Create("winautomation.dll");
    pUiohook_->RegisterMouseMoveCb([&](const long &x, const long &y) {
      std::string output = fmt::format("pt(x:{},y:{})", x, y);
      // std::cout << output << std::endl;
    });

    pUiohook_->RegisterCaptureFinishCb([&](const long &x, const long &y,
                                           bool &exit) {
      /*std::cout << fmt::format("Capture success on point(x:{},y:{}).", x,
         y)
                << std::endl;*/
      /*exit = false;
      auto pElement = pAutomation_->GetElementOnUnderMouse(x, y);
      if (pElement) {
        std::cout << "Found element." << std::endl;
      } else {
        std::cout << "Not found element." << std::endl;
      }*/

      // std::cout << "caprute finished." << std::endl;
      pOverlay_->Screenshot([](const char *buffer, const size_t &buffer_len) {
        // std::cout << buffer_len << std::endl;
        auto pngFilePathname =
            fmt::format(R"(d:\capute_png_s\{}.png)", stl::Time::TimeStamp());
        if (stl::File::WriteFile(pngFilePathname,
                                 std::string(buffer, buffer_len)))
          std::cout << "Write file success." << std::endl;
        else
          std::cout << "Write file failed." << std::endl;
      });
    });
    pAutomation_->RegisterElementCaptureFinishCb([&](const IElement *pElement) {
      if (pElement) {
        auto pos = pElement->GetPosition();
        auto pt = pos->GetPoint();
        std::string output = fmt::format("pt(x:{},y:{}) is one element.",
                                         pt->GetX(), pt->GetY());
        // std::cout << output << std::endl;
        auto rect = pos->GetRect();
        pOverlay_->AppendPos((long)rect->Left(), (long)rect->Top(),
                             (long)rect->Width(), (long)rect->Height());
        pElement->Release();
      }
    });
    ready_.store(true);
  } while (0);
}
void Inspect::UnInit() {
  do {
    if (!ready_.load())
      break;

    if (pOverlay_)
      win::IOverlay::Destroy(&pOverlay_);
    if (pAutomation_)
      win::IAutomation::Destroy(&pAutomation_);
    if (pUiohook_)
      uiohook::IUiohook::Destroy(&pUiohook_);
    ready_.store(false);
  } while (0);
}
bool Inspect::Start() {
  if (pAutomation_)
    pAutomation_->Start();
  if (pOverlay_)
    pOverlay_->Start();
  if (pUiohook_)
    pUiohook_->Start();
  return true;
}
void Inspect::Stop() {
  if (pAutomation_)
    pAutomation_->Stop();
  if (pOverlay_)
    pOverlay_->Stop();
  if (pUiohook_)
    pUiohook_->Stop();
}
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
Inspect *__gpInspect = nullptr;
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpInspect) {
      break;
    }
    __gpInspect = new Inspect();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<Inspection *>(__gpInspect));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpInspect) {
      break;
    }
    delete __gpInspect;
    __gpInspect = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif