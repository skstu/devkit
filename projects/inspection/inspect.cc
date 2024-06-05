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
    // pUiohook_ = uiohook::IUiohook::Create("uiohook.dll");
    // pOverlay_ = win::IOverlay::Create("winoverlay.dll");
    pAutomation_ = win::IAutomation::Create("winautomation.dll");

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
Inspect *__gpInspect = nullptr;
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
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