#include "stdafx.h"

ULONG STDMETHODCALLTYPE Automation::AddRef() {
  return InterlockedIncrement(&refCount);
}

ULONG STDMETHODCALLTYPE Automation::Release() {
  ULONG ret = InterlockedDecrement(&refCount);
  if (ret == 0) {
    delete this;
  }
  return ret;
}

HRESULT STDMETHODCALLTYPE Automation::QueryInterface(REFIID riid,
                                                     void **ppvObject) {
  if (riid == __uuidof(IUnknown) ||
      riid == __uuidof(IUIAutomationFocusChangedEventHandler)) {
    *ppvObject = static_cast<IUIAutomationFocusChangedEventHandler *>(this);
    AddRef();
    return S_OK;
  }
  *ppvObject = nullptr;
  return E_NOINTERFACE;
}
HRESULT STDMETHODCALLTYPE
Automation::HandleFocusChangedEvent(IUIAutomationElement *sender) {
  HRESULT hr;
  do {
    if (!config_->EnableFocusChangedEvent())
      break;
    if (!sender)
      break;
#if 0
    POINT pt;
    if (FALSE == GetCursorPos(&pt))
      break;
    RECT boundingRect;
    if (S_OK != sender->get_CurrentBoundingRectangle(&boundingRect))
      break;
    if (FALSE == PtInRect(&boundingRect, pt))
      break;
#endif
    IUIAutomationElement *clone = nullptr;
    if (S_OK !=
        sender->QueryInterface(IID_IUIAutomationElement, (LPVOID *)&clone))
      break;
    queue_uiautomation_element_.push(clone);
  } while (0);

  return S_OK;
}