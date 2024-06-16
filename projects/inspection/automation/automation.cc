#include "stdafx.h"

//!@ wndInfos->Does not contain the main window handle
static void EnumChildWindows(const HWND &hProcessWnd,
                             std::set<WindowsInfo> &wndInfos);
//!@ wndInfos->Contains the handle to the main window
static void EnumProcessWindows(const DWORD &ProcessId,
                               std::set<WindowsInfo> &wndInfos);

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

    if (S_OK != CoCreateInstance(__uuidof(CUIAutomation), nullptr,
                                 CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                 (void **)&pAutomation_))
      break;

    // pAutomationFocusChangedEvent_ = new AutomationFocusChangedEvent();
    pAutomation_->AddFocusChangedEventHandler(nullptr, this);

    ready_.store(true);
  } while (0);
}
void Automation::UnInit() {
  do {
    pAutomation_->RemoveFocusChangedEventHandler(this);
    // SK_RELEASE_PTR(pAutomationFocusChangedEvent_);
    SK_RELEASE_PTR(pAutomation_);

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
    threads_.emplace_back([this]() { WorkerProc(); });
    threads_.emplace_back([this]() { CallbackProc(); });
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
/*void Automation::OnElementCaptureFinish(const IElement *pElement) const {
  std::unique_lock<std::mutex> lock{*mutex_, std::defer_lock};
  lock.lock();
  if (capture_finish_cb_)
    capture_finish_cb_(pElement);
  lock.unlock();
}
void Automation::RegisterElementCaptureFinishCb(
    const tfElementCaptureFinishCb &cb) {
  std::unique_lock<std::mutex> lock{*mutex_, std::defer_lock};
  lock.lock();
  capture_finish_cb_ = cb;
  lock.unlock();
}
IElement *Automation::GetElementOnUnderMouse(const long &x,
                                             const long &y) const {
  Element *result = nullptr;
  LRESULT hr = S_OK;
  IUIAutomation *pAutomation = nullptr;
  IUIAutomationElement *pElement = nullptr;
  do {
    hr =
        CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(IUIAutomation), (void **)&pAutomation);
    if (FAILED(hr) || !pAutomation)
      break;
    POINT pt = {x, y};
    hr = pAutomation->ElementFromPoint(pt, &pElement);
    if (FAILED(hr) || !pElement)
      break;
    RECT boundingRect = {0};
    hr = pElement->get_CurrentBoundingRectangle(&boundingRect);
    if (FAILED(hr))
      break;
    result = new Element();
    *result << boundingRect;
    result->SetCaprutePoint(pt);
  } while (0);
  SK_RELEASE_PTR(pElement);
  SK_RELEASE_PTR(pAutomation);
  return dynamic_cast<IElement *>(result);
}*/

void Automation::OnCaptureFinished(const Element *pElement) const {
  std::lock_guard<std::mutex> lock{*mutex_};
  for (auto &cb : capture_finished_cbs_) {
    cb.second(cb.first, pElement);
  }
}
void Automation::RegisterCaptureFinishedCb(const unsigned long long &host_ptr,
                                           const tfCaptureFinishedCb &cb) {
  std::lock_guard<std::mutex> lock{*mutex_};
  do {
    if (host_ptr <= 0)
      break;
    auto found = capture_finished_cbs_.find(host_ptr);
    if (found != capture_finished_cbs_.end())
      capture_finished_cbs_.erase(found);
    capture_finished_cbs_.emplace(host_ptr, cb);
  } while (0);
}
void Automation::CallbackProc() {
  RECT rect_prev = {0};

  do {
    do {
      auto eles = queue_uiautomation_element_.pops();
      if (eles.empty())
        break;
      for (auto &ele : eles) {
        do {
          RECT rect;
          rect.left = ele.pos.left;
          rect.top = ele.pos.top;
          rect.right = ele.pos.right;
          rect.bottom = ele.pos.bottom;
          if (memcmp(&rect_prev, &rect, sizeof(RECT)) == 0)
            break;
          memcpy(&rect_prev, &rect, sizeof(RECT));
          OnCaptureFinished(&ele);
        } while (0);
      }
    } while (0);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(thread_loop_interval_callback_));
    if (!open_.load())
      break;
  } while (1);
}
void Automation::WorkerProc() {
  LRESULT hr = S_OK;
  POINT pt_prev = {0};
  POINT pt_next = {0};
  do {
    do {
      if (FALSE == GetCursorPos(&pt_next))
        break;
      if (memcmp(&pt_prev, &pt_next, sizeof(POINT)) == 0)
        break;
      IUIAutomationElement *pUIElement = nullptr;
      hr = pAutomation_->ElementFromPoint(pt_next, &pUIElement);
      if (FAILED(hr) || !pUIElement)
        break;
      Element ele;
      FillElement(pUIElement, ele);
      SK_RELEASE_PTR(pUIElement);
      ele.capture_point.x = pt_next.x;
      ele.capture_point.y = pt_next.y;
      queue_uiautomation_element_.push(ele);
      memcpy(&pt_prev, &pt_next, sizeof(POINT));
    } while (0);
    if (!open_.load())
      break;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(thread_loop_interval_worker_));
  } while (1);
}

void Automation::FillElement(IUIAutomationElement *pElement, Element &element) {
  BSTR bstrValue;
  UINT bstrValueLen = 0;
  VARIANT varValue;
  RECT boundingRect;
  SAFEARRAY *runtimeIdArray = nullptr;
  BOOL bFlag = FALSE;

  //!@ RuntimeId and RuntimeIdLength
  if (SUCCEEDED(pElement->GetRuntimeId(&runtimeIdArray))) {
    LONG lBound, uBound, index;
    if (SUCCEEDED(SafeArrayGetLBound(runtimeIdArray, 1, &lBound)) &&
        SUCCEEDED(SafeArrayGetUBound(runtimeIdArray, 1, &uBound))) {
      element.attr.RuntimeIdLength = uBound - lBound + 1;
      for (index = lBound; index <= uBound && index < 10; ++index) {
        SafeArrayGetElement(runtimeIdArray, &index,
                            &element.attr.RuntimeId[index - lBound]);
      }
    }
    SafeArrayDestroy(runtimeIdArray);
  }

  // ProcessId
  if (SUCCEEDED(pElement->get_CurrentProcessId(&element.attr.ProcessId))) {
  }

  // ControlType and LocalizedControlType
  CONTROLTYPEID controlType;
  if (SUCCEEDED(pElement->get_CurrentControlType(&element.attr.ControlType))) {
    if (SUCCEEDED(pElement->get_CurrentLocalizedControlType(&bstrValue))) {
      bstrValueLen = SysStringLen(bstrValue);
      if (bstrValueLen) {
        wcsncpy(element.attr.LocalizedControlType, bstrValue,
                MAX_ELEMENT_ATTRIBUTES_TEXT);
      }
      SysFreeString(bstrValue);
    }
  }

  // Name
  if (SUCCEEDED(pElement->get_CurrentName(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.Name, bstrValue, MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }
  // AcceleratorKey
  if (SUCCEEDED(pElement->get_CurrentAcceleratorKey(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.AcceleratorKey, bstrValue,
              MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }

  // AccessKey
  if (SUCCEEDED(pElement->get_CurrentAccessKey(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.AccessKey, bstrValue, MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }

  // HasKeyboardFocus
  if (SUCCEEDED(pElement->get_CurrentHasKeyboardFocus(&bFlag))) {
    element.attr.HasKeyboardFocus = bFlag == TRUE ? true : false;
  }

  // IsKeyboardFocusable
  if (SUCCEEDED(pElement->get_CurrentIsKeyboardFocusable(&bFlag))) {
    element.attr.IsKeyboardFocusable = bFlag == TRUE ? true : false;
  }

  // IsEnabled
  if (SUCCEEDED(pElement->get_CurrentIsEnabled(&bFlag))) {
    element.attr.IsEnabled = bFlag == TRUE ? true : false;
  }

  // AutomationId
  if (SUCCEEDED(pElement->get_CurrentAutomationId(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.AutomationId, bstrValue,
              MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }

  // ClassName
  if (SUCCEEDED(pElement->get_CurrentClassName(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.ClassName, bstrValue, MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }

  // HelpText
  if (SUCCEEDED(pElement->get_CurrentHelpText(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.HelpText, bstrValue, MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }

  // Culture
  if (SUCCEEDED(pElement->get_CurrentCulture(&element.attr.Culture))) {
  }

  // IsControlElement
  if (SUCCEEDED(pElement->get_CurrentIsControlElement(&bFlag))) {
    element.attr.IsControlElement = bFlag == TRUE ? true : false;
  }

  // IsContentElement
  if (SUCCEEDED(pElement->get_CurrentIsContentElement(&bFlag))) {
    element.attr.IsContentElement = bFlag == TRUE ? true : false;
  }

  // IsPassword
  if (SUCCEEDED(pElement->get_CurrentIsPassword(&bFlag))) {
    element.attr.IsPassword = bFlag == TRUE ? true : false;
  }

  // ItemType
  if (SUCCEEDED(pElement->get_CurrentItemType(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.ItemType, bstrValue, MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }

  // IsOffscreen
  if (SUCCEEDED(pElement->get_CurrentIsOffscreen(&bFlag))) {
    element.attr.IsOffscreen = bFlag == TRUE ? true : false;
  }

  // FrameworkId
  if (SUCCEEDED(pElement->get_CurrentFrameworkId(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.FrameworkId, bstrValue, MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }

  // IsRequiredForForm
  if (SUCCEEDED(pElement->get_CurrentIsRequiredForForm(&bFlag))) {
    element.attr.IsRequiredForForm = bFlag == TRUE ? true : false;
  }

  // ItemStatus
  if (SUCCEEDED(pElement->get_CurrentItemStatus(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.ItemStatus, bstrValue, MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }

  // AriaRole
  if (SUCCEEDED(pElement->get_CurrentAriaRole(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.AriaRole, bstrValue, MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }

  // AriaProperties
  if (SUCCEEDED(pElement->get_CurrentAriaProperties(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.AriaProperties, bstrValue,
              MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }

  // IsDataValidForForm
  if (SUCCEEDED(pElement->get_CurrentIsDataValidForForm(&bFlag))) {
    element.attr.IsDataValidForForm = bFlag == TRUE ? true : false;
  }

  // ProviderDescription
  if (SUCCEEDED(pElement->get_CurrentProviderDescription(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      wcsncpy(element.attr.ProviderDescription, bstrValue,
              MAX_ELEMENT_ATTRIBUTES_TEXT);
    }
    SysFreeString(bstrValue);
  }
  // Orientation
  OrientationType orientationType;
  if (SUCCEEDED(pElement->get_CurrentOrientation(&orientationType))) {
    element.attr.Orientation = static_cast<int>(orientationType);
  }

  // NativeWindowHandle
  UIA_HWND uiaHwnd = nullptr;
  if (SUCCEEDED(pElement->get_CurrentNativeWindowHandle(&uiaHwnd))) {
    element.attr.NativeWindowHandle =
        reinterpret_cast<decltype(element.attr.NativeWindowHandle)>(uiaHwnd);
  }
  // BoundingRectangle
  if (SUCCEEDED(pElement->get_CurrentBoundingRectangle(&boundingRect))) {
    element.pos.left = boundingRect.left;
    element.pos.top = boundingRect.top;
    element.pos.right = boundingRect.right;
    element.pos.bottom = boundingRect.bottom;
  }
}

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
    if (!sender)
      break;
    POINT pt;
    if (FALSE == GetCursorPos(&pt))
      break;
    Element ele;
    FillElement(sender, ele);
    ele.capture_point.x = pt.x;
    ele.capture_point.y = pt.y;
    queue_uiautomation_element_.push(ele);
  } while (0);

  return S_OK;
}

void EnumProcessWindows(const DWORD &ProcessId,
                        std::set<WindowsInfo> &wndInfos) {
  wndInfos.clear();
  if (ProcessId <= 4)
    return;
  auto tieRoute = std::make_tuple(ProcessId, &wndInfos);
  ::EnumWindows(
      [](HWND hwnd, LPARAM route) -> BOOL {
        auto pRoute =
            reinterpret_cast<std::tuple<DWORD, std::set<WindowsInfo> *> *>(
                route);
        DWORD dwProcessId = std::get<0>(*pRoute);
        std::set<WindowsInfo> *pOutInfos = std::get<1>(*pRoute);
        DWORD thePid = 0;
        ::GetWindowThreadProcessId(hwnd, &thePid);
        if (thePid == dwProcessId) {
          WindowsInfo info;
          info.hWnd = hwnd;
          ::GetWindowTextA(hwnd, info.Text, sizeof(info.Text));
          ::GetClassNameA(hwnd, info.Class, sizeof(info.Class));
          pOutInfos->emplace(info);
#if 0
    (*(HWND*)route) = hwnd;
    return FALSE;
#endif
        }
        return TRUE;
      },
      (LPARAM)&tieRoute);
}
void EnumChildWindows(const HWND &hProcessWnd,
                      std::set<WindowsInfo> &wndInfos) {
  wndInfos.clear();
  if (!hProcessWnd)
    return;
  ::EnumChildWindows(
      hProcessWnd,
      [](HWND hwnd, LPARAM route) -> BOOL {
        auto pOutInfos = reinterpret_cast<std::set<WindowsInfo> *>(route);
        WindowsInfo info;
        info.hWnd = hwnd;
        ::GetWindowTextA(hwnd, info.Text, sizeof(info.Text));
        ::GetClassNameA(hwnd, info.Class, sizeof(info.Class));
        pOutInfos->emplace(info);
        return TRUE;
      },
      (LPARAM)&wndInfos);
}
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
Automation *__gpAutomation = nullptr;
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