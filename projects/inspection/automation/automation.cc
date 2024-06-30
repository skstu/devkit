#include "stdafx.h"

static std::thread gsInitComThread;
static IUIAutomation *gpAutomation = nullptr;
static IUIAutomationElement *gpRootElement = nullptr;
static IUIAutomationTreeWalker *gpAutomationTreeWalker = nullptr;
static IUIAutomationElement *gpUIElementPrev = nullptr;
static IUIAutomationElement *gpUIElementNext = nullptr;
static IUIAutomationCacheRequest *gpUIAutomationCacheRequest = nullptr;
static ULONG_PTR gdiplustoken_ = 0;
static Gdiplus::GdiplusStartupInput gdiplusStartupInput_ = {0};

Automation::Automation() {
  Init();
}
Automation::~Automation() {
  UnInit();
}
void Automation::Release() const {
  delete this;
}
void Automation::FreePtr(void **p) const {
  if (*p) {
    free(*p);
    *p = nullptr;
  }
}
bool Automation::Ready() const {
  return false;
}
void Automation::Init() {
  do {
    if (!GetModuleHandle("win.dll")) {
      is_destroy_win_dll_.store(true);
    }
    pWin_ = win::IWin::Create("win.dll");
    config_ = new Config();
    Gdiplus::GdiplusStartup(&gdiplustoken_, &gdiplusStartupInput_, NULL);
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    ready_.store(true);
  } while (0);
}
void Automation::UnInit() {
  do {
    if (!ready_.load())
      break;
    SK_DELETE_PTR(config_);
    Gdiplus::GdiplusShutdown(gdiplustoken_);
    for (auto &data : map_element_data_)
      data.second->Release();
    if (is_destroy_win_dll_.load())
      win::IWin::Destroy(&pWin_);
    ready_.store(false);
  } while (0);
}
bool Automation::Start() {
  do {
    if (open_.load() || !ready_.load())
      break;
    LRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (S_OK != hr && RPC_E_CHANGED_MODE != hr)
      break;
    coinit_.store(RPC_E_CHANGED_MODE == hr ? false : true);

    if (S_OK != CoCreateInstance(__uuidof(CUIAutomation), NULL,
                                 CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                 (void **)&gpAutomation))
      break;
    if (S_OK != gpAutomation->CreateCacheRequest(&gpUIAutomationCacheRequest))
      break;
    gpUIAutomationCacheRequest->AddProperty(UIA_InvokePatternId);
    gpUIAutomationCacheRequest->AddProperty(UIA_NamePropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_AcceleratorKeyPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_RuntimeIdPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_ProcessIdPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_ControlTypePropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_AccessKeyPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_HasKeyboardFocusPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_IsKeyboardFocusablePropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_IsEnabledPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_AutomationIdPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_ClassNamePropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_HelpTextPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_CulturePropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_IsControlElementPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_IsContentElementPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_IsPasswordPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_ItemTypePropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_IsOffscreenPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_FrameworkIdPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_IsRequiredForFormPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_ItemStatusPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_AriaRolePropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_AriaPropertiesPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_IsDataValidForFormPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_ProviderDescriptionPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_OrientationPropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_NativeWindowHandlePropertyId);
    gpUIAutomationCacheRequest->AddProperty(UIA_BoundingRectanglePropertyId);

    gpUIAutomationCacheRequest->put_TreeScope(TreeScope_Subtree);

    gpAutomation->GetRootElement(&gpRootElement);

    gpAutomation->get_ControlViewWalker(&gpAutomationTreeWalker);

    open_.store(true);

    if (config_->EnableCaptureIs()) {
      threads_.emplace_back([this]() { MouseMoveProc(); });
      threads_.emplace_back([this]() { WorkerProc(); });
      threads_.emplace_back([this]() { CallbackProc(); });
      threads_.emplace_back([this]() { RefreshProc(); });
      threads_.emplace_back([this]() { BuildCacheProc(); });
      if (config_->EnableFocusChangedEventHandler()) {
        gpAutomation->AddFocusChangedEventHandler(NULL, this);
      }
    }
  } while (0);
  return open_.load();
}
void Automation::Stop() {
  do {
    if (!open_.load())
      break;
    open_.store(false);

    if (config_->EnableCaptureIs()) {
      for (auto &t : threads_)
        t.join();
      threads_.clear();
      if (config_->EnableFocusChangedEventHandler()) {
        gpAutomation->RemoveFocusChangedEventHandler(this);
      }
    }

    SK_RELEASE_PTR(pUIElementLast_);
    SK_RELEASE_PTR(gpUIElementNext);
    SK_RELEASE_PTR(gpUIElementPrev);
    SK_RELEASE_PTR(gpRootElement);
    SK_RELEASE_PTR(gpAutomationTreeWalker);
    SK_RELEASE_PTR(gpUIAutomationCacheRequest);
    SK_RELEASE_PTR(gpAutomation);
  } while (0);
  if (coinit_.load())
    CoUninitialize();
}
IElement *Automation::CreateElementFromBin(const char *data,
                                           const size_t &dataLen) const {
  IElement *result = nullptr;
  do {
    if (!data || dataLen != LEN_ELEMENT)
      break;
    ELEMENT element;
    memcpy(&element, data, dataLen);
    if (!element.Verify())
      break;
    result = dynamic_cast<IElement *>(new Element(element));
  } while (0);
  return result;
}
IElement *Automation::CreateElementFromJson(const char *data,
                                            const size_t &dataLen) const {
  IElement *result = nullptr;
  do {
    if (!data || dataLen != LEN_ELEMENT)
      break;

    auto sss = 0;
  } while (0);
  return result;
}
IElement *Automation::FindElementFormDesktop(const IElement *src) const {
  IElement *result = nullptr;
  std::lock_guard<std::mutex> lock{*mutex_};
  IUIAutomationCondition *pConditions = nullptr;
  IUIAutomationElementArray *pFoundElements = nullptr;
  do {
    if (!src)
      break;
    if (!src->Verify())
      break;
    auto found_root = map_element_data_.find(src->Identify());
    if (found_root == map_element_data_.end())
      break;
    pConditions = __CreateCondition(dynamic_cast<const Element *>(src),
                                    ElementFindCondType::Default);
    if (!pConditions)
      break;
    if (!__Refresh())
      break;
    gpRootElement->FindAll(TreeScope_Subtree, pConditions, &pFoundElements);
    if (!pFoundElements)
      break;
    int length = 0;
    pFoundElements->get_Length(&length);
    if (length <= 0)
      break;
    for (int i = 0; i < length; ++i) {
      IUIAutomationElement *pFoundElement = nullptr;
      if (S_OK == pFoundElements->GetElement(i, &pFoundElement)) {
        Element *pElement = new Element(pFoundElement);
        if (!src->Compare(pElement)) {
          result = dynamic_cast<IElement *>(pElement);
          break;
        }
        pElement->Release();
      }
      SK_RELEASE_PTR(pFoundElement);
    }
  } while (0);
  SK_RELEASE_PTR(pConditions);
  SK_RELEASE_PTR(pFoundElements);
  return result;
}
IElement *
Automation::FindElementFormRoot(const IElement *src,
                                const bool &bBringWindowToTopOnRoot) const {

  IElement *result = nullptr;
  std::lock_guard<std::mutex> lock{*mutex_};
  HWND hRoot = nullptr;
  Element *pRootElement = nullptr;
  IUIAutomationElement *pUIRootElement = nullptr;
  IUIAutomationCondition *pConditions = nullptr;
  IUIAutomationElementArray *pFoundElements = nullptr;
  do {
    if (!src)
      break;
    if (!src->Verify())
      break;
    auto found_root = map_element_data_.find(src->Root());
    if (found_root == map_element_data_.end()) {
      std::cout << fmt::format("{} {}", __FUNCTION__,
                               "found_root == map_element_data_.end()")
                << std::endl;
      hRoot = FindWindowW(src->ClassName(), src->Name());
    } else {
      hRoot = FindWindowW(found_root->second->ClassName(),
                          found_root->second->Name());
    }
    // Name = L"Program Manager"
    // ClassName = L"Progman"
    if (!hRoot) {
      std::cout << fmt::format("{} {}", __FUNCTION__, "if (!hRoot)")
                << std::endl;
      break;
    }

    if (bBringWindowToTopOnRoot) {
      ShowWindow(hRoot, SW_SHOW);
      BringWindowToTop(hRoot);
      SetForegroundWindow(hRoot);
      SetWindowPos(hRoot, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    if (!__Refresh())
      break;

    if (S_OK !=
        gpAutomation->ElementFromHandle((UIA_HWND)hRoot, &pUIRootElement))
      break;
    pRootElement = new Element(pUIRootElement);

    pConditions = __CreateCondition(dynamic_cast<const Element *>(src),
                                    ElementFindCondType::Default);
    if (!pConditions)
      break;

    pUIRootElement->FindAll(TreeScope_Subtree, pConditions, &pFoundElements);

    int length = 0;
    pFoundElements->get_Length(&length);
    if (length <= 0)
      break;
    for (int i = 0; i < length; ++i) {
      IUIAutomationElement *pFoundElement = nullptr;
      if (S_OK == pFoundElements->GetElement(i, &pFoundElement)) {
        Element *pElement = new Element(pFoundElement);
        if (!src->Compare(pElement)) {
          result = pElement;
          break;
        }
        pElement->Release();
      }
      SK_RELEASE_PTR(pFoundElement);
    }

    do {
      if (result) {
        std::cout << fmt::format("Found element({}) on '{}'.",
                                 result->Identify(), __FUNCTION__)
                  << std::endl;
        break;
      }
      /*
      If the specified element is not found, a fake element is created
      according to the coordinate offset.
      */
      RECT rtRoot;
      GetWindowRect(hRoot, &rtRoot);
      long offset_x = src->Left() - pRootElement->Left();
      long offset_y = src->Top() - pRootElement->Top();
      result = src->Clone();
      dynamic_cast<Element *>(result)->Fake(true);
      dynamic_cast<Element *>(result)->SetRect(
          rtRoot.left + offset_x, rtRoot.top + offset_y,
          rtRoot.right + offset_x, rtRoot.bottom + offset_y);
      std::cout << fmt::format("Fake element on '{}'.", src->Identify())
                << std::endl;
    } while (0);
    if (bBringWindowToTopOnRoot && hRoot) {
      SetWindowPos(hRoot, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
  } while (0);
  SK_RELEASE_PTR(pConditions);
  SK_RELEASE_PTR(pRootElement);
  SK_RELEASE_PTR(pUIRootElement);
  SK_RELEASE_PTR(pFoundElements);
  return result;
}
bool Automation::__GetElementByFindAll(const Element *src,
                                       IUIAutomationElement **found) const {
  bool result = false;
  *found = nullptr;
  IUIAutomationCondition *pConditions = nullptr;
  IUIAutomationElementArray *pFoundElements = nullptr;
  do {
    if (!src)
      break;
    if (!src->Verify())
      break;
    pConditions = __CreateCondition(src, ElementFindCondType::Default);
    if (!pConditions)
      break;
    if (!__Refresh())
      break;
    if (S_OK !=
        gpRootElement->FindAll(TreeScope_Subtree, pConditions, &pFoundElements))
      break;
    if (!pFoundElements)
      break;
    int length = 0;
    pFoundElements->get_Length(&length);
    if (length <= 0)
      break;
    for (int i = 0; i < length; ++i) {
      IUIAutomationElement *pFoundElement = nullptr;
      if (S_OK == pFoundElements->GetElement(i, &pFoundElement)) {
        Element *pElement = new Element(pFoundElement);
        if (src->Compare(pElement) == 0) {
          *found = pFoundElement;
          result = true;
          break;
        }
        pElement->Release();
      }
      SK_RELEASE_PTR(pFoundElement);
    }
  } while (0);
  SK_RELEASE_PTR(pConditions);
  SK_RELEASE_PTR(pFoundElements);
  return result;
}
bool Automation::__GetElementByPatch(const Element *src,
                                     Element **patch) const {
  bool result = false;
  *patch = nullptr;
  do {
    if (!src)
      break;
    if (!src->Verify())
      break;
    auto found_root = map_element_data_.find(src->Root());
    if (found_root == map_element_data_.end())
      break;

    if (!__Refresh())
      break;

    Element *pRootElement = found_root->second;
    HWND hRoot = FindWindowW(pRootElement->ClassName(), pRootElement->Name());
    if (!hRoot)
      break;
    ShowWindow(hRoot, SW_SHOW);
    BringWindowToTop(hRoot);
    SetForegroundWindow(hRoot);
    SetWindowPos(hRoot, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
#if 0
    pRootConditions = __CreateCondition(found_node_root->second,
                                        ElementFindCondType::Default);
    if (S_OK != gpRootElement->FindFirst(TreeScope_Subtree, pRootConditions,
                                         &pRootElement) ||
        !pRootElement)
      break;

    if (!result && pRootLocalElement && src) {
      /*
      If the specified element is not found, a fake element is created
      according to the coordinate offset.
      */
      RECT rtRoot;
      GetWindowRect(hRoot, &rtRoot);
      long offset_x = src->Left() - pRootLocalElement->Left();
      long offset_y = src->Top() - pRootLocalElement->Top();
      result = src->Clone();
      dynamic_cast<Element *>(result)->SetRect(
          rtRoot.left + offset_x, rtRoot.top + offset_y,
          rtRoot.right + offset_x, rtRoot.bottom + offset_y);
    }
    SetWindowPos(hRoot, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
#endif
    result = true;
  } while (0);
  return result;
}
IElement *
Automation::FindElementFromElement(const IElement *src,
                                   const bool &bBringWindowToTopOnRoot) const {
  std::cout << __FUNCTION__ << std::endl;
  IElement *result = nullptr;
  std::lock_guard<std::mutex> lock{*mutex_};
  IUIAutomationElementArray *pFoundElements = nullptr;
  IUIAutomationCondition *pConditions = nullptr;
  IUIAutomationCondition *pRootConditions = nullptr;
  Element *pRootElement = nullptr;
  HWND hRoot = nullptr;
  do {
    IUIAutomationElement *found = nullptr;
    if (!__GetElementByFindAll((Element *)src, &found))
      break;
    if (result)
      break;
    pRootElement = __GetElementRoot((Element *)src);
    if (!pRootElement)
      break;
    HWND hRoot = FindWindowW(pRootElement->ClassName(), pRootElement->Name());
    if (!hRoot)
      break;
    /*
      If the specified element is not found, a fake element is created
      according to the coordinate offset.
      */
    RECT rtRoot;
    GetWindowRect(hRoot, &rtRoot);
    long offset_x = src->Left() - pRootElement->Left();
    long offset_y = src->Top() - pRootElement->Top();
    result = src->Clone();
    dynamic_cast<Element *>(result)->SetRect(
        rtRoot.left + offset_x, rtRoot.top + offset_y, rtRoot.right + offset_x,
        rtRoot.bottom + offset_y);
  } while (0);

  do {
    if (!src)
      break;
    if (!src->Verify())
      break;
    if (result)
      break;

  } while (0);

  do { //!@ Failure

    auto found_node_root = map_element_data_.find(src->Root());
    if (found_node_root == map_element_data_.end())
      break;
    Element *pRootLocalElement = found_node_root->second;
    HWND hRoot =
        FindWindowW(pRootLocalElement->ClassName(), pRootLocalElement->Name());
    if (!hRoot)
      break;
    ShowWindow(hRoot, SW_SHOW);
    BringWindowToTop(hRoot);
    SetForegroundWindow(hRoot);
    SetWindowPos(hRoot, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    if (bBringWindowToTopOnRoot && hRoot) {
    }

    pRootConditions = __CreateCondition(found_node_root->second,
                                        ElementFindCondType::Default);
    IUIAutomationElement *pFound = nullptr;
    if (S_OK != gpRootElement->FindFirst(TreeScope_Subtree, pRootConditions,
                                         &pFound) ||
        !pFound)
      break;
    std::cout << "Found root element." << std::endl;
    if (!result && pRootLocalElement && src) {
      /*
      If the specified element is not found, a fake element is created
      according to the coordinate offset.
      */
      RECT rtRoot;
      GetWindowRect(hRoot, &rtRoot);
      long offset_x = src->Left() - pRootLocalElement->Left();
      long offset_y = src->Top() - pRootLocalElement->Top();
      result = src->Clone();
      dynamic_cast<Element *>(result)->SetRect(
          rtRoot.left + offset_x, rtRoot.top + offset_y,
          rtRoot.right + offset_x, rtRoot.bottom + offset_y);
    }
    SetWindowPos(hRoot, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  } while (0);
#if 0
  do {
    if (!bBringWindowToTopOnRoot)
      break;
    auto pRoot = __GetElementRoot(pFoundElement);
    if (!pRoot)
      break;
    auto pRootEle = new Element(pRoot);
    HWND hRoot = FindWindowW(pRootEle->ClassName(), pRootEle->Name());
    do {
      if (!hRoot)
        break;
      ShowWindow(hRoot, SW_SHOW);
      BringWindowToTop(hRoot);
      SetForegroundWindow(hRoot);
      SetWindowPos(hRoot, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    } while (0);
    SK_RELEASE_PTR(pRoot);
    SK_RELEASE_PTR(pRootEle);
  } while (0);
#endif
  SK_RELEASE_PTR(pRootElement);
  SK_RELEASE_PTR(pRootConditions);
  SK_RELEASE_PTR(pConditions);
  SK_RELEASE_PTR(pFoundElements);
  return result;
}
bool Automation::Setup(const char *dataPath) {
  bool result = false;
  std::lock_guard<std::mutex> lock{*mutex_};
  map_element_data_.clear();
  do {
    if (!dataPath || !pWin_)
      break;
    auto pDirs = pWin_->EnumFolder(dataPath, false);
    if (!pDirs)
      break;
    const std::string base(dataPath);
    const size_t total = pDirs->Total();
    for (size_t i = 0; i < total; ++i) {
      win::IDirectoryElement *ele = pDirs->Next(i);
      if (!ele)
        continue;
      if (!ele->IsDir())
        continue;
      std::string path =
          fmt::format("{}\\{}\\{}", base, ele->Name(), "ele.bin");
      std::string bin = stl::File::ReadFile(path);
      if (bin.empty() || bin.size() != LEN_ELEMENT)
        continue;
      ELEMENT element;
      memcpy(&element, bin.data(), LEN_ELEMENT);
      if (!element.Verify())
        continue;
      IElement *pElement = dynamic_cast<IElement *>(new Element(element));
      if (!pElement->Verify())
        continue;
      map_element_data_.emplace(pElement->Identify(),
                                dynamic_cast<Element *>(pElement));
      result = true;
    }
  } while (0);
  std::cout << "automation module load eles total = "
            << map_element_data_.size() << std::endl;
  return result;
}
#if 0
IAutomation::IElementArray *
Automation::GetElementFromParent(const Element *parent,
                                 const Element *child) const {
  ElementArray *result = nullptr;
  HRESULT hr = S_OK;
  IUIAutomationElement *pParentElement = nullptr;
  IUIAutomationElementArray *pChildElements = nullptr;
  do {
    if (!parent || !child)
      break;
    // gpAutomationTreeWalker
    pParentElement = __FindElementFirst(parent, TreeScope_Subtree);
    if (!pParentElement)
      break;

    IUIAutomationCondition *pConditions =
        __GenerateCondition(child, ElementFindCondType::Default);
    if (!pConditions)
      break;
    if (S_OK != pParentElement->FindAll(TreeScope_Children, pConditions,
                                        &pChildElements))
      break;
    if (!pChildElements)
      break;

    int length = 0;
    pChildElements->get_Length(&length);
    for (int i = 0; i < length; ++i) {
      IUIAutomationElement *pFoundElement = nullptr;
      do {
        if (S_OK != pChildElements->GetElement(i, &pFoundElement))
          break;
        Element FoundElement;
        FillElement(pFoundElement, FoundElement);
        if (!result)
          result = new ElementArray();
        result->Push(FoundElement);
      } while (0);
      SK_RELEASE_PTR(pFoundElement);
    }
  } while (0);
  SK_RELEASE_PTR(pChildElements);
  SK_RELEASE_PTR(pParentElement);
  return result;
}

IAutomation::IElementArray *
Automation::FindElementAll(const Element *src,
                           const ElementFindCondType &conds) const {
  ElementArray *result = nullptr;
  HRESULT hr = S_OK;
  IUIAutomationCondition *pConditions = nullptr;
  IUIAutomationElementArray *pFoundElements = nullptr;
  do {
    if (!src)
      break;
    if (!__Refresh())
      break;

    pConditions = __GenerateCondition(src, conds);
    if (!pConditions)
      break;

    if (S_OK !=
        gpRootElement->FindAll(TreeScope_Subtree, pConditions, &pFoundElements))
      break;
    if (!pFoundElements)
      break;

    int length = 0;
    pFoundElements->get_Length(&length);
    for (int i = 0; i < length; ++i) {
      IUIAutomationElement *pFoundElement = nullptr;
      do {
        if (S_OK != pFoundElements->GetElement(i, &pFoundElement))
          break;
        Element FoundElement;
        FillElement(pFoundElement, FoundElement);
        if (!result)
          result = new ElementArray();
        result->Push(FoundElement);
      } while (0);
      SK_RELEASE_PTR(pFoundElement);
    }

    // result = true;
  } while (0);
  SK_RELEASE_PTR(pFoundElements);

  return dynamic_cast<IAutomation::IElementArray *>(result);
}

IUIAutomationElement *
Automation::__FindElementFirst(const Element *src, const TreeScope &treeScope,
                               const ElementFindCondType &conds) const {
  IUIAutomationElement *result = nullptr;
  IUIAutomationCondition *pConditions = nullptr;
  do {
    if (!src)
      break;
    if (!__Refresh())
      break;
    pConditions = __GenerateCondition(src, conds);
    if (!pConditions)
      break;
    if (S_OK != gpRootElement->FindFirst(treeScope, pConditions, &result))
      break;
    //!@ Success
  } while (0);
  SK_RELEASE_PTR(pConditions);
  return result;
}

#endif
IElement *Automation::GetElementLast() const {
  Element *result = nullptr;
  std::unique_lock<std::mutex> lock{*mutex_, std::defer_lock};
  lock.lock();
  result = new Element(pUIElementLast_);
  lock.unlock();
  return dynamic_cast<IElement *>(result);
}
Element *Automation::__GetElementRoot(const Element *src) const {
  Element *result = nullptr;
  IUIAutomationElement *pRootElement = nullptr;
  IUIAutomationCondition *pRootConditions = nullptr;
  do {
    if (!src || !src->Verify())
      break;
    auto found_node_root = map_element_data_.find(src->Root());
    if (found_node_root == map_element_data_.end())
      break;
    pRootConditions = __CreateCondition(found_node_root->second,
                                        ElementFindCondType::Default);
    if (!pRootConditions)
      break;
    if (S_OK != gpRootElement->FindFirst(TreeScope_Subtree, pRootConditions,
                                         &pRootElement) ||
        !pRootElement)
      break;
    result = new Element(pRootElement);
  } while (0);
  SK_RELEASE_PTR(pRootElement);
  SK_RELEASE_PTR(pRootConditions);
  return result;
}
IUIAutomationElement *
Automation::__GetElementRoot(IUIAutomationElement *src) const {
  IUIAutomationElement *result = src;
  do {
    if (!src)
      break;
    IUIAutomationElement *pElementNext = nullptr;
    IUIAutomationElement *pElementPrev = nullptr;
    if (S_OK !=
        src->QueryInterface(IID_IUIAutomationElement, (LPVOID *)&pElementNext))
      break;
    int pid_next = 0;
    int pid_prev = 0;

    do {
      if (!pElementNext)
        break;
      pElementNext->get_CurrentProcessId(&pid_next);
      if (pid_prev <= 0 && pid_next)
        pid_prev = pid_next;
      if (pid_prev && pid_next && pid_prev != pid_next) {
        break;
      }

      if (S_OK != gpAutomationTreeWalker->GetParentElement(pElementNext,
                                                           &pElementPrev) ||
          !pElementPrev)
        break;
      pElementNext->QueryInterface(IID_IUIAutomationElement, (LPVOID *)&result);
      SK_RELEASE_PTR(pElementNext);
      pElementNext = pElementPrev;
    } while (1);

    SK_RELEASE_PTR(pElementPrev);
  } while (0);
  return result;
}
Element *Automation::__CreateElement(IUIAutomationElement *pUIElement) const {
  Element *result = nullptr;
  do {
    if (!pUIElement)
      break;
    IElementArray *chain = __GetElementChain(pUIElement);
    if (!chain)
      break;
    result = new Element(pUIElement);

  } while (0);
  return result;
}
ElementArray *
Automation::__GetElementChain(IUIAutomationElement *pUIElement) const {
  ElementArray *result = nullptr;
  do {
    if (!pUIElement)
      break;
    if (!__Refresh())
      break;

    IUIAutomationElement *pElementNext = nullptr;
    IUIAutomationElement *pElementPrev = nullptr;
    if (S_OK != pUIElement->QueryInterface(IID_IUIAutomationElement,
                                           (LPVOID *)&pElementNext))
      break;
    int pid_next = 0;
    int pid_prev = 0;

    do {
      if (!pElementNext)
        break;
      pElementNext->get_CurrentProcessId(&pid_next);
      if (!result)
        result = new ElementArray();

      if (pid_prev <= 0 && pid_next)
        pid_prev = pid_next;
      if (pid_prev && pid_next && pid_prev != pid_next)
        break;
      result->Push(pElementNext);
      if (S_OK != gpAutomationTreeWalker->GetParentElement(pElementNext,
                                                           &pElementPrev) ||
          !pElementPrev)
        break;
      SK_RELEASE_PTR(pElementNext);
      pElementNext = pElementPrev;
    } while (1);

    SK_RELEASE_PTR(pElementPrev);
  } while (0);
  return result;
}
IElementArray *Automation::GetElementCapture() const {
  ElementArray *result = nullptr;
  std::unique_lock<std::mutex> lock{*mutex_, std::defer_lock};
  lock.lock();
  do {
    if (!pUIElementLast_)
      break;
    if (!__Refresh())
      break;
    IUIAutomationElement *pElementNext = nullptr;
    IUIAutomationElement *pElementPrev = nullptr;
    if (S_OK != pUIElementLast_->QueryInterface(IID_IUIAutomationElement,
                                                (LPVOID *)&pElementNext))
      break;
    int pid_next = 0;
    int pid_prev = 0;

    do {
      if (!pElementNext)
        break;
      pElementNext->get_CurrentProcessId(&pid_next);
      if (!result)
        result = new ElementArray();

      if (pid_prev <= 0 && pid_next)
        pid_prev = pid_next;
      if (pid_prev && pid_next && pid_prev != pid_next) {
        result->Fixed(pElementNext);
        break;
      }

      Element *current = result->Push(pElementNext);

      if (S_OK != gpAutomationTreeWalker->GetParentElement(pElementNext,
                                                           &pElementPrev) ||
          !pElementPrev)
        break;
      SK_RELEASE_PTR(pElementNext);
      pElementNext = pElementPrev;
    } while (1);

    SK_RELEASE_PTR(pElementPrev);
  } while (0);
  lock.unlock();
  return dynamic_cast<IElementArray *>(result);
}
IElement *Automation::GetElementFromHwnd(const void *hwnd) const {
  IElement *result = nullptr;
  std::lock_guard<std::mutex> lock{*mutex_};
  LRESULT hr = S_OK;
  do {
    if (!gpAutomation)
      break;
    IUIAutomationElement *pElement = nullptr;
    hr = gpAutomation->ElementFromHandle((UIA_HWND)hwnd, &pElement);
    if (FAILED(hr))
      break;
    // FillElement(pElement, *element);
    result = dynamic_cast<IElement *>(new Element(pElement));
    SK_RELEASE_PTR(pElement);
  } while (0);
  return result;
}
bool Automation::__Refresh() const {
  bool result = false;
  HRESULT hr = S_OK;
  do {
    if (!ready_.load())
      break;
    SK_RELEASE_PTR(gpRootElement);
    hr = gpAutomation->GetRootElement(&gpRootElement);
    if (FAILED(hr))
      break;
    SK_RELEASE_PTR(gpAutomationTreeWalker);
    hr = gpAutomation->get_ControlViewWalker(&gpAutomationTreeWalker);
    if (FAILED(hr))
      break;
    result = true;
  } while (0);
  return result;
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

// 检测矩形A是否完全包含在矩形B中
static bool IsRectInRect(const RECT &rectA, const RECT &rectB) {
  return (rectA.left >= rectB.left && rectA.top >= rectB.top &&
          rectA.right <= rectB.right && rectA.bottom <= rectB.bottom);
}
void Automation::__EnumerateCacheElement(
    IUIAutomationElement *pCacheElement,
    std::vector<IUIAutomationElement *> &all) const {
  IUIAutomationElementArray *pElementArray = nullptr;
  do {
    if (!pCacheElement)
      break;

    if (S_OK != pCacheElement->FindAllBuildCache(TreeScope_Children, NULL,
                                                 gpUIAutomationCacheRequest,
                                                 &pElementArray) ||
        !pElementArray)
      break;
    int length;
    pElementArray->get_Length(&length);
    if (length <= 0)
      break;
    for (int i = 0; i < length; i++) {
      IUIAutomationElement *pChildElement = nullptr;
      pElementArray->GetElement(i, &pChildElement);
      if (!pChildElement)
        continue;
      bool __break = false;
      all.emplace_back(pChildElement);
      if (__break)
        break;
      __EnumerateCacheElement(pChildElement, all);
    }

  } while (0);
  SK_RELEASE_PTR(pElementArray);
}
void Automation::__EnumerateElement(
    IUIAutomationTreeWalker *pTreeWalker, IUIAutomationElement *pTargetElement,
    const std::function<void(IUIAutomationElement *, bool &)> &enum_cb) const {
  IUIAutomationElement *pChild = nullptr;
  do {
    if (!pTargetElement || !enum_cb || !pTreeWalker)
      break;
    pTreeWalker->GetFirstChildElement(pTargetElement, &pChild);
    do {
      if (!pChild)
        break;
      bool __break = false;
      enum_cb(pChild, __break);
      if (__break)
        break;

      __EnumerateElement(pTreeWalker, pChild, enum_cb);

      IUIAutomationElement *pNextSibling = nullptr;
      pTreeWalker->GetNextSiblingElement(pChild, &pNextSibling);
      SK_RELEASE_PTR(pChild);
      pChild = pNextSibling;
    } while (1);

  } while (0);
  SK_RELEASE_PTR(pChild);
}
void Automation::RefreshProc() {
  // element_catch_map_.clear();
  do {
    do {
      if (!__Refresh())
        break;
      uint64_t __begin = stl::Time::TimeStamp<std::chrono::milliseconds>();
      IUIAutomationElement *pCacheDesktopElement = nullptr;
      gpRootElement->BuildUpdatedCache(gpUIAutomationCacheRequest,
                                       &pCacheDesktopElement);
      if (!pCacheDesktopElement)
        break;

      uint64_t __end = stl::Time::TimeStamp<std::chrono::milliseconds>();

      // std::cout << __end - __begin << std::endl;
      auto sssksk = 0;
    } while (0);

#if 0
    do {
      if (!__Refresh())
        break;
      IUIAutomationElement *pFoundElement = nullptr;
      RECT rtFoundElement = {0};
      size_t total = 0;
      __EnumerateElement(gpAutomationTreeWalker, gpRootElement,
                         [&](IUIAutomationElement *pElement, bool &__break) {
                           ++total;
                           do {
                             __break = !open_.load();
                             auto pLocalElement = new Element(pElement);
                             element_catch_map_.push(
                                 pLocalElement->Identify(), pLocalElement,
                                 [](const Element *exists) {
                                   auto sk = 0;
                                   auto ks = 0;
                                   std::cout << exists->Identify() << std::endl;
                                 });
                           } while (0);
                           // Note: Do not release !!!
                           // SK_RELEASE_PTR(pElement);
                         });
      std::cout << total << std::endl;

    } while (0);
#endif
    if (!open_.load())
      break;

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  } while (1);
}
bool Automation::__DingHandle() {
  bool result = false;
  IUIAutomationElement *pElementDingMain = nullptr;
  std::lock_guard<std::mutex> lock{*mutex_};
  do {
    HWND hDingTalk = FindWindowW(L"StandardFrame_DingTalk", L"钉钉");
    if (!hDingTalk)
      break;
    if (!IsWindowVisible(hDingTalk))
      break;
    POINT pt = {0};
    GetCursorPos(&pt);
    RECT rtDingMain;
    GetWindowRect(hDingTalk, &rtDingMain);
    if (PtInRect(&rtDingMain, pt) == FALSE)
      break;
    if (!open_.load())
      break;
    if (S_OK !=
        gpAutomation->ElementFromHandle((UIA_HWND)hDingTalk, &pElementDingMain))
      break;
    __Refresh();

    IUIAutomationElement *pFoundElement = nullptr;
    RECT rtFoundElement = {0};
    __EnumerateElement(
        gpAutomationTreeWalker, pElementDingMain,
        [&](IUIAutomationElement *pElement, bool &__break) {
          do {
            __break = !open_.load();
            RECT boundingRect;
            if (S_OK != pElement->get_CurrentBoundingRectangle(&boundingRect))
              break;
            if (PtInRect(&boundingRect, pt) == FALSE)
              break;

            long area_prev = (rtFoundElement.right - rtFoundElement.left) *
                             (rtFoundElement.bottom - rtFoundElement.top);
            long area_next = (boundingRect.right - boundingRect.left) *
                             (boundingRect.bottom - boundingRect.top);
            if (area_prev <= 0 || (area_prev > area_next)) {
              SK_RELEASE_PTR(pFoundElement);
              pElement->QueryInterface(IID_IUIAutomationElement,
                                       (LPVOID *)&pFoundElement);
              memcpy(&rtFoundElement, &boundingRect, sizeof(RECT));
            }
          } while (0);
        });
    if (!pFoundElement)
      break;

    queue_uiautomation_element_.push(pFoundElement);
    result = true;
  } while (0);
  SK_RELEASE_PTR(pElementDingMain);
  return result;
}
void Automation::CallbackProc() {
  RECT rect_prev = {0};
  do {
    do {
      auto uiEles = queue_uiautomation_element_.pops();
      if (uiEles.empty()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(thread_loop_interval_callback_));
        break;
      }

      IUIAutomationElement *last = *std::prev(uiEles.end());

      do {
        RECT boundingRect;
        if (S_OK != last->get_CurrentBoundingRectangle(&boundingRect))
          break;
        if (memcmp(&rect_prev, &boundingRect, sizeof(RECT)) == 0)
          break;
        /*if (IsRectInRect(boundingRect, rect_prev))
          break;*/
        std::unique_lock<std::mutex> lock{*mutex_, std::defer_lock};
        lock.lock();
        SK_RELEASE_PTR(pUIElementLast_);
        IUIAutomationElement *clone = nullptr;
        last->QueryInterface(IID_IUIAutomationElement,
                             (LPVOID *)&pUIElementLast_);
        IElement *pElement = new Element(last);
        for (auto &cb : capture_finished_cbs_) {
          cb.second(cb.first, dynamic_cast<IElement *>(pElement));
        }
        SK_RELEASE_PTR(pElement);
        lock.unlock();

        memcpy(&rect_prev, &boundingRect, sizeof(RECT));
      } while (0);
      for (auto &ele : uiEles)
        SK_RELEASE_PTR(ele);
      uiEles.clear();

    } while (0);
    if (!open_.load()) {
      break;
    } else {
      //__DingHandle();
    }
  } while (1);
}
IAutomation::IConfig *Automation::ConfigGet() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return dynamic_cast<IConfig *>(config_);
}
void Automation::OnMouseMove() {
  std::lock_guard<std::mutex> lock{*mutex_};

  LRESULT hr = S_OK;
  do {
    if (!config_->EnableMouseMoveEvent())
      break;
    POINT pt = {0};
    if (FALSE == GetCursorPos(&pt))
      break;
    hr = gpAutomation->ElementFromPoint(pt, &gpUIElementNext);
    if (FAILED(hr) || !gpUIElementNext)
      break;
    if (gpUIElementPrev) {
      BOOL areSame = FALSE;
      hr = gpAutomation->CompareElements(gpUIElementPrev, gpUIElementNext,
                                         &areSame);
      if (FAILED(hr)) {
        SK_RELEASE_PTR(gpUIElementPrev);
        SK_RELEASE_PTR(gpUIElementNext);
        break;
      }
      if (areSame == TRUE) {
        SK_RELEASE_PTR(gpUIElementNext);
        break;
      }
    }
    if (!gpUIElementNext)
      break;
    IUIAutomationElement *clone = nullptr;
    if (S_OK == gpUIElementNext->QueryInterface(IID_IUIAutomationElement,
                                                (LPVOID *)&clone)) {
      queue_uiautomation_element_.push(clone);
    }
    SK_RELEASE_PTR(gpUIElementPrev);
    gpUIElementPrev = gpUIElementNext;
  } while (0);
}
void Automation::OnMouseClicked() {
  std::lock_guard<std::mutex> lock{*mutex_};
  LRESULT hr = S_OK;
  do {
    if (!config_->EnableMouseClickedEvent())
      break;
    POINT pt = {0};
    if (FALSE == GetCursorPos(&pt))
      break;
    IUIAutomationElement *pUIElement = nullptr;
    hr = gpAutomation->ElementFromPoint(pt, &pUIElement);
    if (FAILED(hr) || !pUIElement)
      break;
    queue_uiautomation_element_.push(pUIElement);
  } while (0);
}

void PrintElementTree(IUIAutomationElement *pElement) {
  do {
    if (!pElement)
      break;
    IUIAutomationElementArray *pElementArray = nullptr;
    pElement->FindAllBuildCache(TreeScope_Children, NULL,
                                gpUIAutomationCacheRequest, &pElementArray);
    if (!pElementArray)
      break;
    int length;
    pElementArray->get_Length(&length);
    if (length <= 0)
      break;
    for (int i = 0; i < length; i++) {
      IUIAutomationElement *pChildElement = nullptr;
      pElementArray->GetElement(i, &pChildElement);

      PrintElementTree(pChildElement);
    }
  } while (0);
}

void Automation::BuildCacheProc() {
  do {
    do {
      std::vector<HWND> tasks = queue_build_cache_.pops();
      if (tasks.empty())
        break;
      size_t task_count = tasks.size();
      for (const auto &task : tasks) {
        --task_count;
        IUIAutomationElement *pTaskElement = nullptr;
        IUIAutomationElement *pCacheElement = nullptr;
        do {
          gpAutomation->ElementFromHandleBuildCache(
              reinterpret_cast<UIA_HWND>(task), gpUIAutomationCacheRequest,
              &pTaskElement);
          /*gpAutomation->ElementFromHandle(reinterpret_cast<UIA_HWND>(task),
                                          &pTaskElement);*/
          if (!pTaskElement)
            break;

          uint64_t __build_cache_begin =
              stl::Time::TimeStamp<std::chrono::milliseconds>();

          pTaskElement->BuildUpdatedCache(gpUIAutomationCacheRequest,
                                          &pCacheElement);
          if (!pCacheElement)
            break;
          uint64_t __build_cache_end =
              stl::Time::TimeStamp<std::chrono::milliseconds>();

          uint64_t __iterator_build_cache_element_begin = stl::Time::TimeStamp<
              std::chrono::milliseconds>(); // map_hwnd_build_cache_

          // PrintElementTree(pCacheElement);
          std::vector<IUIAutomationElement *> all;
          __EnumerateCacheElement(pCacheElement, all);
          __EnumerateCacheElement(pTaskElement, all);
          const size_t total = all.size();
          if (total) {
            auto sk = 0;
          }
          uint64_t __iterator_build_cache_element_end =
              stl::Time::TimeStamp<std::chrono::milliseconds>();

          std::cout << fmt::format("BuildUpdateCache task({}) buildTimes({}) "
                                   "itTimes({}) total({}).",
                                   task_count,
                                   __build_cache_end - __build_cache_begin,
                                   __iterator_build_cache_element_end -
                                       __iterator_build_cache_element_begin,
                                   total)
                    << std::endl;
        } while (0);
        SK_RELEASE_PTR(pTaskElement);
        SK_RELEASE_PTR(pCacheElement);
      }

    } while (0);
    if (!open_.load())
      break;
  } while (1);
}
bool Automation::__BuildUpdatedCache(const HWND &hWnd) {
  bool result = false;
  do {
    if (!hWnd)
      break;
    IUIAutomationElement *pElement = nullptr;
    gpAutomation->ElementFromHandle(reinterpret_cast<UIA_HWND>(hWnd),
                                    &pElement);
    if (!pElement)
      break;

    uint64_t __begin = stl::Time::TimeStamp<std::chrono::milliseconds>();
    IUIAutomationElement *pCacheDesktopElement = nullptr;
    pElement->BuildUpdatedCache(gpUIAutomationCacheRequest,
                                &pCacheDesktopElement);

    uint64_t __end = stl::Time::TimeStamp<std::chrono::milliseconds>();
    // std::cout << __end - __begin << std::endl;
    result = true;
  } while (0);
  return result;
}
void Automation::MouseMoveProc() {
  POINT pt_prev = {0};
  POINT pt_next = {0};

  const HWND hDesktop = GetDesktopWindow();
  UIA_HWND hCurrent = nullptr;
  IUIAutomationElement *pElementCurrentPoint = nullptr;
  do {
    do {
      if (!config_->EnableMouseMoveEvent())
        break;
      if (FALSE == GetCursorPos(&pt_next))
        break;
      if (memcmp(&pt_prev, &pt_next, sizeof(POINT)) == 0)
        break;
      gpAutomation->ElementFromPoint(pt_next, &pElementCurrentPoint);
      if (!pElementCurrentPoint)
        break;
      pElementCurrentPoint->get_CurrentNativeWindowHandle(&hCurrent);
      if (hCurrent) {
        HWND hCurrentAncestor =
            GetAncestor(reinterpret_cast<HWND>(hCurrent), GA_ROOT);
        char className[MAX_PATH];
        char windowText[MAX_PATH];
        GetClassNameA(hCurrentAncestor, className, MAX_PATH);
        GetWindowTextA(hCurrentAncestor, windowText, MAX_PATH);
        std::cout << className << " " << windowText << std::endl;
        /*if (hCurrentAncestor == hDesktop)
          break;*/
        queue_build_cache_.push(hCurrentAncestor);
      }
      queue_uiautomation_element_.push(pElementCurrentPoint);
      memcpy(&pt_prev, &pt_next, sizeof(POINT));
    } while (0);
    if (!open_.load())
      break;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(thread_loop_interval_worker_));
  } while (1);
}
void Automation::WorkerProc() {
  return;
  LRESULT hr = S_OK;
  POINT pt_prev = {0};
  POINT pt_next = {0};
  RECT boundingRectPrev = {0};
  do {
    do {
      if (!config_->EnableMouseMoveEvent())
        break;
      if (FALSE == GetCursorPos(&pt_next))
        break;
      if (memcmp(&pt_prev, &pt_next, sizeof(POINT)) == 0)
        break;
      IUIAutomationElement *pElementPoint = nullptr;
      IUIAutomationElement *pElementFocused = nullptr;
      hr = gpAutomation->ElementFromPoint(pt_next, &pElementPoint);
      // hr = gpAutomation->GetFocusedElement(&pElementFocused);
      if (!pElementPoint)
        break;
#if 0
      RECT boundingRect;
      if (S_OK != pElementPoint->get_CurrentBoundingRectangle(&boundingRect))
        break;
      if (!IsRectInRect(boundingRect, boundingRectPrev)) {
        SK_RELEASE_PTR(pElementPoint);
        break;
      }
#endif
      queue_uiautomation_element_.push(pElementPoint);
      /*if (pElementFocused)
        queue_uiautomation_element_.push(pElementFocused);*/
      memcpy(&pt_prev, &pt_next, sizeof(POINT));
      // memcpy(&boundingRectPrev, &boundingRect, sizeof(RECT));
    } while (0);
    if (!open_.load())
      break;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(thread_loop_interval_worker_));
  } while (1);
}

IUIAutomationCondition *
Automation::__CreateCondition(const Element *ele,
                              const ElementFindCondType &conds) const {
  const IElement *pElement = dynamic_cast<const IElement *>(ele);
  IUIAutomationCondition *result = nullptr;
  VARIANT var;
  SAFEARRAY *condition_pas = nullptr;
  IUIAutomationCondition *pCondition = nullptr;
  std::vector<IUIAutomationCondition *> conditions;
  do {
    if (!pElement)
      return nullptr;

    do { //!@AutomationId
      if (!(conds & ElementFindCondType::AutomationId))
        break;
      if (!__ConvertToVariant(pElement->AutomationId(), var))
        break;

      if (!gpAutomation)
        break;
      if (S_OK != gpAutomation->CreatePropertyCondition(
                      UIA_AutomationIdPropertyId, var, &pCondition)) {
        break;
      }
      conditions.emplace_back(pCondition);
    } while (0);

    do { //!@Name
      if (!(conds & ElementFindCondType::Name))
        break;
      if (!__ConvertToVariant(pElement->Name(), var))
        break;
      pCondition = nullptr;
      if (S_OK != gpAutomation->CreatePropertyCondition(UIA_NamePropertyId, var,
                                                        &pCondition))
        break;
      conditions.emplace_back(pCondition);
    } while (0);

    do { //!@ControlType
      if (!(conds & ElementFindCondType::ControlType))
        break;

      VariantInit(&var);
      V_VT(&var) = VT_I4;
      V_I4(&var) = pElement->ControlType();
      if (S_OK != gpAutomation->CreatePropertyCondition(
                      UIA_ControlTypePropertyId, var, &pCondition))
        break;

      conditions.emplace_back(pCondition);
    } while (0);

    do { //!@ClassName
      if (!(conds & ElementFindCondType::ClassName))
        break;
      if (pElement->ClassName()[0] == 0)
        break;
      if (!__ConvertToVariant(pElement->ClassName(), var))
        break;
      if (S_OK != gpAutomation->CreatePropertyCondition(UIA_ClassNamePropertyId,
                                                        var, &pCondition))
        break;
      conditions.emplace_back(pCondition);
    } while (0);

    do { //!@FrameworkId
      if (!(conds & ElementFindCondType::FrameworkId))
        break;
      if (pElement->FrameworkId()[0] == 0)
        break;
      if (!__ConvertToVariant(pElement->FrameworkId(), var))
        break;
      if (S_OK != gpAutomation->CreatePropertyCondition(
                      UIA_FrameworkIdPropertyId, var, &pCondition))
        break;
      conditions.emplace_back(pCondition);
    } while (0);

    do { //!@IsControlElement
      if (!(conds & ElementFindCondType::IsControlElement))
        break;
      VariantInit(&var);
      V_VT(&var) = VT_BOOL;
      V_I4(&var) =
          pElement->IsControlElement() <= 0 ? VARIANT_FALSE : VARIANT_TRUE;
      if (S_OK != gpAutomation->CreatePropertyCondition(
                      UIA_IsControlElementPropertyId, var, &pCondition))
        break;
      conditions.emplace_back(pCondition);
    } while (0);

    if (conditions.empty())
      break;

    condition_pas = SafeArrayCreateVector(VT_UNKNOWN, 0, conditions.size());
    if (!condition_pas)
      break;
    for (LONG i = 0; i < conditions.size(); ++i) {
      if (S_OK != SafeArrayPutElement(condition_pas, &i, conditions[i]))
        continue;
    }
    if (S_OK !=
        gpAutomation->CreateAndConditionFromArray(condition_pas, &result))
      break;

    //!@ Success
  } while (0);

  for (auto &cond : conditions)
    SK_RELEASE_PTR(cond);
  if (condition_pas)
    SafeArrayDestroy(condition_pas);
  VariantClear(&var);
  return result;
}
bool Automation::__ConvertToVariant(const wchar_t *src, VARIANT &dst) const {
  bool result = false;
  do {
    VariantInit(&dst);
    dst.vt = VT_EMPTY;
    if (!src) {
      result = true;
      break;
    }
    dst.vt = VT_BSTR;
    dst.bstrVal = SysAllocString(src);
    if (!dst.bstrVal) {
      VariantClear(&dst);
      break;
    }
    result = true;
  } while (0);
  return result;
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
  SK_DELETE_PTR(__gpAutomation);
}

#ifdef __cplusplus
}
#endif