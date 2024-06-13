#include "stdafx.h"

AutomationFocusChangedEvent::AutomationFocusChangedEvent() : refCount(1) {
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

  /*if (sender != nullptr) {
    BSTR name;
    BSTR automationId;
    CONTROLTYPEID controlType;

    sender->get_CurrentName(&name);
    sender->get_CurrentAutomationId(&automationId);
    sender->get_CurrentControlType(&controlType);

    std::wcout << L"Focus changed to: Name=" << name << L", ControlType="
               << controlType << L", AutomationId=" << automationId
               << std::endl;

    SysFreeString(name);
    SysFreeString(automationId);
  } else {
    std::wcout << L"Focus changed to a null element." << std::endl;
  }*/

  return S_OK;
}

#if 0
int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IUIAutomation* automation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, reinterpret_cast<void**>(&automation));

    if (SUCCEEDED(hr)) {
        AutomationFocusChangedEvent* handler = new AutomationFocusChangedEvent();
        hr = automation->AddFocusChangedEventHandler(nullptr, handler);
        handler->Release();

        if (SUCCEEDED(hr)) {
            std::wcout << L"Press Enter to exit..." << std::endl;
            std::wcin.get();

            automation->RemoveFocusChangedEventHandler(handler);
        } else {
            std::wcout << L"Failed to add focus changed event handler." << std::endl;
        }

        automation->Release();
    } else {
        std::wcout << L"Failed to initialize UIAutomation." << std::endl;
    }

    CoUninitialize();
    return 0;
}
#endif
