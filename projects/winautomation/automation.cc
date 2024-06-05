#include "stdafx.h"

static IUIAutomation *gpAutomation = nullptr;

Automation::Automation() {
  Init();
}
Automation::~Automation() {
  UnInit();
}
void Automation::Release() const {
  delete this;
}
bool Automation::Ready() const {
  return false;
}
void Automation::Init() {
  do {
    LRESULT coret = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (S_OK != coret && RPC_E_CHANGED_MODE != coret)
      break;
    coinit_.store(RPC_E_CHANGED_MODE == coret ? false : true);
    ready_.store(true);
  } while (0);
}
void Automation::UnInit() {
  do {
    if (coinit_.load()) {
      CoUninitialize();
    }
    ready_.store(false);
  } while (0);
}
bool Automation::Start() {
  do {
    if (open_.load() || !ready_.load())
      break;

    open_.store(true);
    threads_.emplace_back([this]() { Process(); });
  } while (0);
  return open_.load();
}
void Automation::Stop() {
  do {
    if (!open_.load())
      break;
    open_.store(false);
    for (auto &t : threads_)
      t.join();
    threads_.clear();
  } while (0);
}
void Automation::Process() {
  LRESULT hr = S_OK;
  IUIAutomation *pAutomation = nullptr;
  IUIAutomationElement *pElement = nullptr;
  do {
    SK_RELEASE_PTR(pElement);
    SK_RELEASE_PTR(pAutomation);
    do {
      hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr,
                            CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                            (void **)&pAutomation);
      if (FAILED(hr) || !pAutomation)
        break;
      POINT pt = {0};
      if (FALSE == GetCursorPos(&pt))
        break;
      hr = pAutomation->ElementFromPoint(pt, &pElement);
      if (FAILED(hr) || !pElement)
        break;
      RECT boundingRect = {0};
      hr = pElement->get_CurrentBoundingRectangle(&boundingRect);
      if (FAILED(hr))
        break;
      OnCapruteFinish(nullptr);
    } while (0);
    if (!open_.load())
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  } while (1);
  SK_RELEASE_PTR(pElement);
  SK_RELEASE_PTR(pAutomation);
}
void Automation::OnCapruteFinish(const IAutomation::IElement *pElement) const {
  std::unique_lock<std::mutex> lock{*mutex_, std::defer_lock};
  lock.lock();
  if (caprute_finish_cb_) {
    caprute_finish_cb_(pElement);
  }
  lock.unlock();
}
void Automation::RegisterCapruteFinishCb(const tfCapruteFinishCb &cb) {
  std::unique_lock<std::mutex> lock{*mutex_, std::defer_lock};
  lock.lock();
  caprute_finish_cb_ = cb;
  lock.unlock();
}
Automation *__gpAutomation = nullptr;
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpAutomation)
      break;

    __gpAutomation = new Automation();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<IAutomation *>(__gpAutomation));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpAutomation)
      break;

    delete __gpAutomation;
    __gpAutomation = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif