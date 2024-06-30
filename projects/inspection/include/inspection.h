#ifndef __INSPECTION_H_
#define __INSPECTION_H_

#include "interface.h"
#include "libuiohook.h"

namespace inspect {
const size_t MAX_ELEMENT_ATTRIBUTES_TEXT = 256;

enum class OverlayWindowType : unsigned long long {
  OVERLAY_WINDOW_BK = 1,
  OVERLAY_WINDOW_CV = OVERLAY_WINDOW_BK << 1,
  OVERLAY_WINDOW_UI = OVERLAY_WINDOW_BK << 2,
  OVERLAY_WINDOW_ELE = OVERLAY_WINDOW_BK << 3,

  OVERLAY_ALL = OVERLAY_WINDOW_BK | OVERLAY_WINDOW_CV | OVERLAY_WINDOW_UI |
                OVERLAY_WINDOW_ELE,
};
static bool operator&(const OverlayWindowType &t1,
                      const OverlayWindowType &t2) {
  return static_cast<unsigned long long>(t1) &
         static_cast<unsigned long long>(t2);
}
static OverlayWindowType operator|(const OverlayWindowType &t1,
                                   const OverlayWindowType &t2) {
  return static_cast<OverlayWindowType>(static_cast<unsigned long long>(t1) |
                                        static_cast<unsigned long long>(t2));
}

enum class ElementFindCondType : unsigned long long {
  ControlType = 1,
  AutomationId = ControlType << 1,
  ClassName = ControlType << 2,
  IsControlElement = ControlType << 3,
  Name = ControlType << 4,
  FrameworkId = ControlType << 5,
  NativeWindowHandle = ControlType << 6,
  ProcessId = ControlType << 7,

  Default = ControlType | AutomationId | ClassName | IsControlElement |
            FrameworkId | Name,
  All = Default | NativeWindowHandle | ProcessId,
};
static bool operator&(const ElementFindCondType &t1,
                      const ElementFindCondType &t2) {
  return static_cast<unsigned long long>(t1) &
         static_cast<unsigned long long>(t2);
}
static ElementFindCondType operator|(const ElementFindCondType &t1,
                                     const ElementFindCondType &t2) {
  return static_cast<ElementFindCondType>(static_cast<unsigned long long>(t1) |
                                          static_cast<unsigned long long>(t2));
}

class IElement {
public:
  virtual const char *RuntimeId() const = 0;
  virtual int ProcessId() const = 0;
  virtual int ControlType() const = 0;
  virtual const wchar_t *LocalizedControlType() const = 0;
  virtual const wchar_t *Name() const = 0;
  virtual const wchar_t *AcceleratorKey() const = 0;
  virtual const wchar_t *AccessKey() const = 0;
  virtual bool HasKeyboardFocus() const = 0;
  virtual bool IsKeyboardFocusable() const = 0;
  virtual bool IsEnabled() const = 0;
  virtual const wchar_t *AutomationId() const = 0;
  virtual const wchar_t *ClassName() const = 0;
  virtual const wchar_t *HelpText() const = 0;
  virtual int Culture() const = 0;
  virtual bool IsControlElement() const = 0;
  virtual bool IsContentElement() const = 0;
  virtual bool IsPassword() const = 0;
  virtual unsigned long long NativeWindowHandle() const = 0;
  virtual const wchar_t *ItemType() const = 0;
  virtual bool IsOffscreen() const = 0;
  virtual int Orientation() const = 0;
  virtual const wchar_t *FrameworkId() const = 0;
  virtual bool IsRequiredForForm() const = 0;
  virtual const wchar_t *ItemStatus() const = 0;
  virtual const wchar_t *AriaRole() const = 0;
  virtual const wchar_t *AriaProperties() const = 0;
  virtual bool IsDataValidForForm() const = 0;
  virtual const wchar_t *ProviderDescription() const = 0;

  virtual long Left() const = 0;
  virtual long Top() const = 0;
  virtual long Right() const = 0;
  virtual long Bottom() const = 0;
  virtual long Width() const = 0;
  virtual long Height() const = 0;
  virtual long CenterX() const = 0;
  virtual long CenterY() const = 0;

  virtual void Release() const = 0;
  virtual IElement *Clone() const = 0;
  virtual const char *Parent() const = 0;
  virtual const char *Root() const = 0;
  virtual const char *Identify() const = 0;
  virtual const char *Sha1() const = 0;
  virtual const char *Sha256() const = 0;
  virtual bool IsRoot() const = 0;
  virtual bool IsFake() const = 0;
  virtual bool TopMost() const = 0;
  virtual bool Verify() const = 0;
  virtual int Compare(const IElement *) const = 0;
  virtual void Screenshot() = 0;
  virtual unsigned int Total() const = 0;
  virtual bool ImageStream(char **, size_t *) const = 0;
  virtual void Serialization(char **, size_t *) const = 0;
};

class IElementArray {
public:
  virtual IElement *Begin() const = 0;
  virtual IElement *End() const = 0;
  virtual void Release() const = 0;
  virtual size_t Total() const = 0;
  virtual IElement *Next(const size_t &) const = 0;
};
class IAutomation : public Interface<IAutomation> {
public:
  using tfCaptureFinishedCb = void (*)(const unsigned long long &,
                                       const IElement *);
  class IConfig {
  public:
    virtual void EnableFocusChangedEvent(const bool &) = 0;
    virtual void EnableMouseClickedEvent(const bool &) = 0;
    virtual void EnableMouseMoveEvent(const bool &) = 0;
    virtual bool EnableFocusChangedEvent() const = 0;
    virtual bool EnableMouseClickedEvent() const = 0;
    virtual bool EnableMouseMoveEvent() const = 0;
    virtual void EnableCapture(const bool &) = 0;
    virtual bool EnableCaptureIs() const = 0;
    virtual void EnableFocusChangedEventHandler(const bool &) = 0;
    virtual bool EnableFocusChangedEventHandler() const = 0;
  };

public:
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual bool Setup(const char *) = 0;
  virtual bool Ready() const = 0;
  virtual void FreePtr(void **) const = 0;
  virtual void Release() const = 0;
  virtual IConfig *ConfigGet() const = 0;
  virtual void OnMouseClicked() = 0;
  virtual void OnMouseMove() = 0;
  virtual void RegisterCaptureFinishedCb(const unsigned long long &,
                                         const tfCaptureFinishedCb &) = 0;
  virtual IElement *GetElementFromHwnd(const void *hwnd) const = 0;
  virtual IElement *
  FindElementFromElement(const IElement *,
                         const bool &bBringWindowToTopOnRoot) const = 0;
  virtual IElement *
  FindElementFormRoot(const IElement *,
                      const bool &bBringWindowToTopOnRoot) const = 0;
  virtual IElement *FindElementFormDesktop(const IElement *) const = 0;
  virtual IElement *GetElementLast() const = 0;
  virtual IElementArray *GetElementCapture() const = 0;
  virtual IElement *CreateElementFromBin(const char *,
                                         const size_t &) const = 0;
  virtual IElement *CreateElementFromJson(const char *,
                                          const size_t &) const = 0;

#if 0
  virtual IElementArray *FindElementAll(const Element *,
                                        const ElementFindCondType &) const = 0;
  //!@ Note! This method may block the process !
  
  virtual IElementArray *GetElementFromParent(const Element *parent,
                                              const Element *child) const = 0;

#endif
};

class Inspection : public Interface<Inspection> {
public:
  class IConfig {
  public:
  };

public:
  virtual IConfig *ConfigGet() const = 0;
  virtual void Stop() = 0;
  virtual bool Start() = 0;
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
};

class IOverlay : public Interface<IOverlay> {
public:
  class IConfig {
  public:
    virtual void EnableScreenshot(const bool &) = 0;
    virtual void EnableDrawBorder(const bool &) = 0;
    virtual void SetBorderARGB(const unsigned short &a, const unsigned short &r,
                               const unsigned short &g,
                               const unsigned short &b) = 0;
    virtual void SetLabel(const wchar_t *) = 0;
  };
  class IStream {
  public:
    virtual void SetData(const void *, const size_t &) = 0;
    virtual void *GetBuffer() const = 0;
    virtual size_t GetSize() const = 0;
    virtual void Release() const = 0;
    virtual bool Empty() const = 0;
  };
  using tfScreenshotFinishedCb = void (*)(const OverlayWindowType &,
                                          const IStream *);

public:
  virtual IConfig *ConfigGet() const = 0;
  virtual bool Screenshot(const OverlayWindowType &) const = 0;
  virtual bool Start(const OverlayWindowType &) = 0;
  virtual void Stop() = 0;
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
  virtual void OnUioEvent(const uiohook::IUioEvent *) const = 0;
  virtual void SetPosition(const long &x, const long &y, const long &cx,
                           const long &cy) const = 0;
  virtual void RegisterScreenshotFinishedCb(const tfScreenshotFinishedCb &) = 0;
};

class ICapture : public Interface<ICapture> {
public:
  virtual void Stop() = 0;
  virtual bool Start() = 0;
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
};

} // namespace inspect

#endif //__INSPECTION_H_