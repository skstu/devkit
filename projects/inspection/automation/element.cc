#include "stdafx.h"

static void __safe_wmemcpy(wchar_t *to, const size_t &to_size,
                           const wchar_t *from, const size_t &from_size) {
  if (from && from_size > 0 && to_size > 0 && to) {
    wmemcpy(to, from, from_size > to_size ? to_size : from_size);
  }
}
static void __safe_memcpy(char *to, const size_t &to_size, const char *from,
                          const size_t &from_size) {
  if (from && from_size > 0 && to_size > 0 && to) {
    memcpy(to, from, from_size > to_size ? to_size : from_size);
  }
}

Element::Element() {
}
Element::Element(const tagElement &ele) {
  *this << ele;
}
Element::Element(IUIAutomationElement *pUIElement) {
  *this << pUIElement;
}
Element::~Element() {
}
void Element::Release() const {
  delete this;
}
IElement *Element::Clone() const {
  return dynamic_cast<IElement *>(new Element(element_));
}
const char *Element::RuntimeId() const {
  return element_.runtimeid;
}
int Element::ProcessId() const {
  return element_.attr.ProcessId;
}
int Element::ControlType() const {
  return element_.attr.ControlType;
}
const wchar_t *Element::LocalizedControlType() const {
  return element_.attr.LocalizedControlType;
}
const wchar_t *Element::Name() const {
  return element_.attr.Name;
}
const wchar_t *Element::AcceleratorKey() const {
  return element_.attr.AcceleratorKey;
}
const wchar_t *Element::AccessKey() const {
  return element_.attr.AccessKey;
}
bool Element::HasKeyboardFocus() const {
  return element_.attr.HasKeyboardFocus ? true : false;
}
bool Element::IsKeyboardFocusable() const {
  return element_.attr.IsKeyboardFocusable ? true : false;
}
bool Element::IsEnabled() const {
  return element_.attr.IsEnabled ? true : false;
}
const wchar_t *Element::AutomationId() const {
  return element_.attr.AutomationId;
}
const wchar_t *Element::ClassName() const {
  return element_.attr.ClassName;
}
const wchar_t *Element::HelpText() const {
  return element_.attr.HelpText;
}
int Element::Culture() const {
  return element_.attr.Culture;
}
bool Element::IsControlElement() const {
  return element_.attr.IsControlElement ? true : false;
}
bool Element::IsContentElement() const {
  return element_.attr.IsContentElement ? true : false;
}
bool Element::IsPassword() const {
  return element_.attr.IsPassword ? true : false;
}
unsigned long long Element::NativeWindowHandle() const {
  return element_.attr.NativeWindowHandle;
}
const wchar_t *Element::ItemType() const {
  return element_.attr.ItemType;
}
bool Element::IsOffscreen() const {
  return element_.attr.IsOffscreen ? true : false;
}
int Element::Orientation() const {
  return element_.attr.Orientation;
}
const wchar_t *Element::FrameworkId() const {
  return element_.attr.FrameworkId;
}
bool Element::IsRequiredForForm() const {
  return element_.attr.IsRequiredForForm ? true : false;
}
const wchar_t *Element::ItemStatus() const {
  return element_.attr.ItemStatus;
}
const wchar_t *Element::AriaRole() const {
  return element_.attr.AriaRole;
}
const wchar_t *Element::AriaProperties() const {
  return element_.attr.AriaProperties;
}
bool Element::IsDataValidForForm() const {
  return element_.attr.IsDataValidForForm ? true : false;
}
const wchar_t *Element::ProviderDescription() const {
  return element_.attr.ProviderDescription;
}

long Element::Left() const {
  return element_.pos.left;
}
long Element::Top() const {
  return element_.pos.top;
}
long Element::Right() const {
  return element_.pos.right;
}
long Element::Bottom() const {
  return element_.pos.bottom;
}
long Element::Width() const {
  return element_.pos.right - element_.pos.left;
}
long Element::Height() const {
  return element_.pos.bottom - element_.pos.top;
}
long Element::CenterX() const {
  return element_.pos.left + Width() / 2;
}
long Element::CenterY() const {
  return element_.pos.top + Height() / 2;
}

const char *Element::Parent() const {
  return element_.parent;
}
const char *Element::Root() const {
  return element_.root;
}
const char *Element::Identify() const {
  return element_.md5;
}
const char *Element::Sha1() const {
  return element_.hmac_sha1;
}
const char *Element::Sha256() const {
  return element_.hmac_sha256;
}
void Element::GetRect(tagRECT &rect) const {
  rect.left = element_.pos.left;
  rect.top = element_.pos.top;
  rect.right = element_.pos.right;
  rect.bottom = element_.pos.bottom;
}
void Element::SetParent(IElement *pParent) {
  if (pParent) {
    memcpy(element_.parent, pParent->Identify(), sizeof(element_.parent));
  }
}
void Element::SetRoot(IElement *pRoot) {
  if (pRoot) {
    memcpy(element_.root, pRoot->Identify(), sizeof(element_.root));
  }
}
void Element::SetRect(const long &left, const long &top, const long &right,
                      const long &bottom) {
  element_.pos.left = left;
  element_.pos.top = top;
  element_.pos.right = right;
  element_.pos.bottom = bottom;
}
int Element::Compare(const IElement *pElement) const {
  int result = -1;
  do {
    if (!pElement)
      break;
    if (memcmp(element_.md5, pElement->Identify(), sizeof(element_.md5)) != 0)
      break;
    result = 0;
  } while (0);
  return result;
}
unsigned int Element::Total() const {
  return LEN_ELEMENT;
}
void Element::Serialization(char **data, size_t *len) const {
  *len = LEN_ELEMENT;
  *data = (char *)malloc(*len);
  memcpy(*data, &element_, *len);
}
bool Element::ImageStream(char **data, size_t *len) const {
  bool result = false;
  *len = imgBuffer_.size();
  *data = nullptr;
  do {
    if (imgBuffer_.empty())
      break;
    *data = (char *)malloc(*len);
    memcpy(*data, imgBuffer_.data(), *len);
    result = true;
  } while (0);
  return result;
}
void Element::operator<<(const tagElement &ele) {
  element_ = ele;
}
void Element::operator<<(IUIAutomationElement *pElement) {
  if (!pElement)
    return;
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
      element_.attr.RuntimeIdLength = uBound - lBound + 1;
      for (index = lBound; index <= uBound && index < 10; ++index) {
        SafeArrayGetElement(runtimeIdArray, &index,
                            &element_.attr.RuntimeId[index - lBound]);
      }
    }

    std::string hex = stl::String::BinaryToHexString(std::string(
        (char *)&element_.attr.RuntimeId[0],
        element_.attr.RuntimeIdLength * sizeof(element_.attr.RuntimeId[0])));

    __safe_memcpy(element_.runtimeid, sizeof(element_.runtimeid), hex.data(),
                  hex.size());
    SafeArrayDestroy(runtimeIdArray);
  }

  // ProcessId
  if (SUCCEEDED(pElement->get_CurrentProcessId(&element_.attr.ProcessId))) {
  }

  // ControlType and LocalizedControlType
  CONTROLTYPEID controlType;
  if (SUCCEEDED(pElement->get_CurrentControlType(&element_.attr.ControlType))) {
    if (SUCCEEDED(pElement->get_CurrentLocalizedControlType(&bstrValue))) {
      bstrValueLen = SysStringLen(bstrValue);
      if (bstrValueLen) {
        __safe_wmemcpy(element_.attr.LocalizedControlType,
                       sizeof(element_.attr.LocalizedControlType) /
                           sizeof(wchar_t),
                       bstrValue, bstrValueLen);
      }
      SysFreeString(bstrValue);
    }
  }

  // Name
  if (SUCCEEDED(pElement->get_CurrentName(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.Name,
                     sizeof(element_.attr.Name) / sizeof(wchar_t), bstrValue,
                     bstrValueLen);
    }
    SysFreeString(bstrValue);
  }
  // AcceleratorKey
  if (SUCCEEDED(pElement->get_CurrentAcceleratorKey(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.AcceleratorKey,
                     sizeof(element_.attr.AcceleratorKey) / sizeof(wchar_t),
                     bstrValue, bstrValueLen);
    }
    SysFreeString(bstrValue);
  }

  // AccessKey
  if (SUCCEEDED(pElement->get_CurrentAccessKey(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.AccessKey,
                     sizeof(element_.attr.AccessKey) / sizeof(wchar_t),
                     bstrValue, bstrValueLen);
    }
    SysFreeString(bstrValue);
  }

  // HasKeyboardFocus
  if (SUCCEEDED(pElement->get_CurrentHasKeyboardFocus(&bFlag))) {
    element_.attr.HasKeyboardFocus = bFlag == TRUE ? 1 : 0;
  }

  // IsKeyboardFocusable
  if (SUCCEEDED(pElement->get_CurrentIsKeyboardFocusable(&bFlag))) {
    element_.attr.IsKeyboardFocusable = bFlag == TRUE ? 1 : 0;
  }

  // IsEnabled
  if (SUCCEEDED(pElement->get_CurrentIsEnabled(&bFlag))) {
    element_.attr.IsEnabled = bFlag == TRUE ? 1 : 0;
  }

  // AutomationId
  if (SUCCEEDED(pElement->get_CurrentAutomationId(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.AutomationId,
                     sizeof(element_.attr.AutomationId) / sizeof(wchar_t),
                     bstrValue, bstrValueLen);
    }
    SysFreeString(bstrValue);
  }

  // ClassName
  if (SUCCEEDED(pElement->get_CurrentClassName(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.ClassName,
                     sizeof(element_.attr.ClassName) / sizeof(wchar_t),
                     bstrValue, bstrValueLen);
    }
    SysFreeString(bstrValue);
  }

  // HelpText
  if (SUCCEEDED(pElement->get_CurrentHelpText(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.HelpText,
                     sizeof(element_.attr.HelpText) / sizeof(wchar_t),
                     bstrValue, bstrValueLen);
    }
    SysFreeString(bstrValue);
  }

  // Culture
  if (SUCCEEDED(pElement->get_CurrentCulture(&element_.attr.Culture))) {
  }

  // IsControlElement
  if (SUCCEEDED(pElement->get_CurrentIsControlElement(&bFlag))) {
    element_.attr.IsControlElement = bFlag == TRUE ? 1 : 0;
  }

  // IsContentElement
  if (SUCCEEDED(pElement->get_CurrentIsContentElement(&bFlag))) {
    element_.attr.IsContentElement = bFlag == TRUE ? 1 : 0;
  }

  // IsPassword
  if (SUCCEEDED(pElement->get_CurrentIsPassword(&bFlag))) {
    element_.attr.IsPassword = bFlag == TRUE ? 1 : 0;
  }

  // ItemType
  if (SUCCEEDED(pElement->get_CurrentItemType(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.ItemType,
                     sizeof(element_.attr.ItemType) / sizeof(wchar_t),
                     bstrValue, bstrValueLen);
    }
    SysFreeString(bstrValue);
  }

  // IsOffscreen
  if (SUCCEEDED(pElement->get_CurrentIsOffscreen(&bFlag))) {
    element_.attr.IsOffscreen = bFlag == TRUE ? 1 : 0;
  }

  // FrameworkId
  if (SUCCEEDED(pElement->get_CurrentFrameworkId(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.FrameworkId,
                     sizeof(element_.attr.FrameworkId) / sizeof(wchar_t),
                     bstrValue, bstrValueLen);
    }
    SysFreeString(bstrValue);
  }

  // IsRequiredForForm
  if (SUCCEEDED(pElement->get_CurrentIsRequiredForForm(&bFlag))) {
    element_.attr.IsRequiredForForm = bFlag == TRUE ? 1 : 0;
  }

  // ItemStatus
  if (SUCCEEDED(pElement->get_CurrentItemStatus(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.ItemStatus,
                     sizeof(element_.attr.ItemStatus) / sizeof(wchar_t),
                     bstrValue, bstrValueLen);
    }
    SysFreeString(bstrValue);
  }

  // AriaRole
  if (SUCCEEDED(pElement->get_CurrentAriaRole(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.AriaRole,
                     sizeof(element_.attr.AriaRole) / sizeof(wchar_t),
                     bstrValue, bstrValueLen);
    }
    SysFreeString(bstrValue);
  }

  // AriaProperties
  if (SUCCEEDED(pElement->get_CurrentAriaProperties(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.AriaProperties,
                     sizeof(element_.attr.AriaProperties) / sizeof(wchar_t),
                     bstrValue, bstrValueLen);
    }
    SysFreeString(bstrValue);
  }

  // IsDataValidForForm
  if (SUCCEEDED(pElement->get_CurrentIsDataValidForForm(&bFlag))) {
    element_.attr.IsDataValidForForm = bFlag == TRUE ? 1 : 0;
  }

  // ProviderDescription
  if (SUCCEEDED(pElement->get_CurrentProviderDescription(&bstrValue))) {
    bstrValueLen = SysStringLen(bstrValue);
    if (bstrValueLen) {
      __safe_wmemcpy(element_.attr.ProviderDescription,
                     sizeof(element_.attr.ProviderDescription) /
                         sizeof(wchar_t),
                     bstrValue, bstrValueLen);
    }
    SysFreeString(bstrValue);
  }
  // Orientation
  OrientationType orientationType;
  if (SUCCEEDED(pElement->get_CurrentOrientation(&orientationType))) {
    element_.attr.Orientation = static_cast<int>(orientationType);
  }

  // NativeWindowHandle
  UIA_HWND uiaHwnd = nullptr;
  if (SUCCEEDED(pElement->get_CurrentNativeWindowHandle(&uiaHwnd))) {
    element_.attr.NativeWindowHandle =
        reinterpret_cast<decltype(element_.attr.NativeWindowHandle)>(uiaHwnd);
  }
  // BoundingRectangle
  if (SUCCEEDED(pElement->get_CurrentBoundingRectangle(&boundingRect))) {
    element_.pos.left = boundingRect.left;
    element_.pos.top = boundingRect.top;
    element_.pos.right = boundingRect.right;
    element_.pos.bottom = boundingRect.bottom;
  }

  Final();
}

void Element::Screenshot() {
  std::vector<char> imgBuffer;
  imgBuffer.clear();
  IStream *pStream = nullptr;
  HBITMAP hBitmap = nullptr;
  HDC hScreenDC = nullptr;
  HDC hMemoryDC = nullptr;
  do {
    if (Width() <= 0 && Height() <= 0)
      break;
    hScreenDC = GetDC(NULL);
    hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hScreenDC || !hMemoryDC)
      break;
    hBitmap = CreateCompatibleBitmap(hScreenDC, Width(), Height());
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    if (!hBitmap || !hOldBitmap)
      break;
    if (FALSE == BitBlt(hMemoryDC, 0, 0, Width(), Height(), hScreenDC, Left(),
                        Top(), SRCCOPY))
      break;
    Gdiplus::Bitmap bitmap(hBitmap, NULL);
    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &pStream)) || !pStream)
      break;
    CLSID clsid;
    if (FAILED(
            CLSIDFromString(L"{557CF400-1A04-11D3-9A73-0000F81EF32E}", &clsid)))
      break;
    if (Gdiplus::Status::Ok != bitmap.Save(pStream, &clsid, NULL))
      break;
    STATSTG statstg = {0};
    if (FAILED(pStream->Stat(&statstg, STATFLAG_NONAME)))
      break;
    ULONG streamSize = statstg.cbSize.LowPart;
    imgBuffer_.resize(streamSize);
    //*len = streamSize;
    //*buffer = (char *)malloc(*len);
    LARGE_INTEGER liZero = {};
    pStream->Seek(liZero, STREAM_SEEK_SET, NULL);
    ULONG bytesRead = 0;
    pStream->Read(&imgBuffer_[0], streamSize, &bytesRead);
    imgBuffer_.resize(bytesRead);
    SelectObject(hMemoryDC, hOldBitmap);
  } while (0);
  if (hBitmap)
    DeleteObject(hBitmap);
  if (hMemoryDC)
    DeleteDC(hMemoryDC);
  if (hScreenDC)
    ReleaseDC(NULL, hScreenDC);
  SK_RELEASE_PTR(pStream);
}
bool Element::IsFake() const {
  return fake_.load();
}
void Element::Fake(const bool &flag) {
  fake_.store(flag);
}
bool Element::TopMost() const {
  bool result = false;
  do {
    HWND hWnd = FindWindowW(ClassName(), Name());
    if (hWnd)
      break;
    ShowWindow(hWnd, SW_SHOW);
    BringWindowToTop(hWnd);
    SetForegroundWindow(hWnd);
    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    result = true;
  } while (0);
  return result;
}
bool Element::Verify() const {
  return element_.Verify();
}
bool Element::IsRoot() const {
  return memcmp(element_.root, element_.parent, sizeof(element_.root)) == 0;
}
void Element::Final() {

  /*
  在使用 UI Automation
  进行跨机器自动化时，重要的是选择那些在不同机器上仍然保持一致的属性。并不是所有的
  UI Automation
  属性都是不变的或者在跨机器环境中保持一致。以下是一些常见的、通常跨机器保持一致的属性：

  1. **AutomationId** (`UIA_AutomationIdPropertyId`):
    - AutomationId
  是一个唯一标识符，用于标识应用程序内的某个元素。如果应用程序设计良好，这个
  ID 在不同的机器上应该是一致的。

  2. **Name** (`UIA_NamePropertyId`):
    - Name 是一个人类可读的字符串，描述 UI
  元素的名称。这个属性通常在跨机器环境中也是一致的，尤其是对于文本标签和按钮等。

  3. **ControlType** (`UIA_ControlTypePropertyId`):
    - ControlType 表示 UI
  元素的类型，例如按钮、复选框、编辑框等。这个属性在不同机器上应该是一致的。

  4. **ClassName** (`UIA_ClassNamePropertyId`):
    - ClassName 通常是控件的类名，这个属性在不同机器上也应该是一致的。

  5. **LocalizedControlType** (`UIA_LocalizedControlTypePropertyId`):
    - LocalizedControlType 是 ControlType
  的本地化字符串，通常也一致，但取决于系统语言设置。

  6. **FrameworkId** (`UIA_FrameworkIdPropertyId`):
    - FrameworkId 标识 UI 元素所属的框架，例如 "Win32", "WPF", "WinForm"
  等。这个属性通常也一致。

  7. **IsControlElement** (`UIA_IsControlElementPropertyId`):
    - 表示元素是否是 UI 控件。这个属性一般也保持一致。
  */
  // Calc hash [AutomationId + ControlType + Name + ClassName + FrameworkId +
  // IsControlElement]
  //!@ element_.attr.AutomationId
  do {
    std::string final_string;
    //!@ AutomationId
    final_string.append((char *)element_.attr.AutomationId,
                        sizeof(element_.attr.AutomationId));
    //!@ ControlType
    final_string.append((char *)&element_.attr.ControlType,
                        sizeof(element_.attr.ControlType));
    //!@ Name
    final_string.append((char *)element_.attr.Name, sizeof(element_.attr.Name));
    //!@ ClassName
    final_string.append((char *)element_.attr.ClassName,
                        sizeof(element_.attr.ClassName));
    //!@ FrameworkId
    final_string.append((char *)element_.attr.FrameworkId,
                        sizeof(element_.attr.FrameworkId));
    //!@ IsControlElement
    final_string.append((char *)&element_.attr.IsControlElement,
                        sizeof(element_.attr.IsControlElement));

    final_string.insert(0, std::string((char *)&element_.__begin__,
                                       sizeof(element_.__begin__)));
    final_string.append(
        std::string((char *)&element_.__end__, sizeof(element_.__end__)));

    std::string final_md5 = ossl::Ssl::MD5(final_string);
    if (!final_md5.empty())
      __safe_memcpy(element_.md5, sizeof(element_.md5), final_md5.data(),
                    final_md5.size());
    std::string final_hmac_sha1 = ossl::Ssl::HMAC_SHA1(final_string, "");
    if (!final_hmac_sha1.empty())
      __safe_memcpy(element_.hmac_sha1, sizeof(element_.hmac_sha1),
                    final_hmac_sha1.data(), final_hmac_sha1.size());
    std::string final_hmac_sha256 = ossl::Ssl::HMAC_SHA256(final_string, "");
    if (!final_hmac_sha256.empty())
      __safe_memcpy(element_.hmac_sha256, sizeof(element_.hmac_sha256),
                    final_hmac_sha256.data(), final_hmac_sha256.size());
  } while (0);
}
///////////////////////////////////////////////////////////////////////////////////
tagElement::tagElement() {
  memset(this, 0x00, sizeof(*this));
  __begin__ = 0xFAC9C2D0;
  __end__ = 0xB4B4AAC1;
}
bool tagElement::Verify() const {
  return __begin__ == 0xFAC9C2D0 && __end__ == 0xB4B4AAC1;
}
tagElement::~tagElement() {
}
void tagElement::operator=(const tagElement &obj) {
  memcpy(this, &obj, sizeof(*this));
}
bool tagElement::operator==(const tagElement &obj) const {
  return memcmp(md5, obj.md5, sizeof(md5)) == 0;
}
bool tagElement::operator!=(const tagElement &obj) const {
  return !(*this == obj);
}
bool tagElement::operator>(const tagElement &obj) const {
  return memcmp(md5, obj.md5, sizeof(md5)) > 0;
}
bool tagElement::operator<(const tagElement &obj) const {
  return memcmp(md5, obj.md5, sizeof(md5)) < 0;
}