#include "stdafx.h"

AutomationFocusChangedEvent::AutomationFocusChangedEvent() {
}

AutomationFocusChangedEvent::~AutomationFocusChangedEvent() {
}

ULONG STDMETHODCALLTYPE AutomationFocusChangedEvent::AddRef() {
  return InterlockedIncrement(&refCount);
}

ULONG STDMETHODCALLTYPE AutomationFocusChangedEvent::Release() {
  ULONG ret = InterlockedDecrement(&refCount);
  if (ret == 0) {
    delete this;
  }
  return ret;
}

HRESULT STDMETHODCALLTYPE
AutomationFocusChangedEvent::QueryInterface(REFIID riid, void **ppvObject) {
  if (riid == __uuidof(IUnknown) ||
      riid == __uuidof(IUIAutomationFocusChangedEventHandler)) {
    *ppvObject = static_cast<IUIAutomationFocusChangedEventHandler *>(this);
    AddRef();
    return S_OK;
  }
  *ppvObject = nullptr;
  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE AutomationFocusChangedEvent::HandleFocusChangedEvent(
    IUIAutomationElement *sender) {
  HRESULT hr;
  do {
    if (!sender)
      break;
    POINT pt;
    if (FALSE == GetCursorPos(&pt))
      break;
    Element ele;
    Automation::FillElement(sender, ele);
    ele.capture_point.x = pt.x;
    ele.capture_point.y = pt.y;
    __gpAutomation->queue_uiautomation_element_.push(ele);
  } while (0);

  return S_OK;
}
