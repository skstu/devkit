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

  OVERLAY_ALL = OVERLAY_WINDOW_BK | OVERLAY_WINDOW_CV | OVERLAY_WINDOW_UI,
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

#pragma pack(push, 1)
typedef struct tagElement {
  typedef struct {
    long x;
    long y;
  } Point;
  typedef struct {
    long left;
    long top;
    long right;
    long bottom;
  } Position;
  typedef struct AutomationElement {
    int RuntimeId[32];
    int RuntimeIdLength;
    int ProcessId;
    int ControlType;
    wchar_t LocalizedControlType[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t Name[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t AcceleratorKey[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t AccessKey[MAX_ELEMENT_ATTRIBUTES_TEXT];
    bool HasKeyboardFocus;
    bool IsKeyboardFocusable;
    bool IsEnabled;
    wchar_t AutomationId[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t ClassName[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t HelpText[MAX_ELEMENT_ATTRIBUTES_TEXT];
    int Culture;
    bool IsControlElement;
    bool IsContentElement;
    bool IsPassword;
    unsigned long long NativeWindowHandle;
    wchar_t ItemType[MAX_ELEMENT_ATTRIBUTES_TEXT];
    bool IsOffscreen;
    int Orientation;
    wchar_t FrameworkId[MAX_ELEMENT_ATTRIBUTES_TEXT];
    bool IsRequiredForForm;
    wchar_t ItemStatus[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t AriaRole[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t AriaProperties[MAX_ELEMENT_ATTRIBUTES_TEXT];
    bool IsDataValidForForm;
    wchar_t ProviderDescription[MAX_ELEMENT_ATTRIBUTES_TEXT]; // BSTR
  } Attributes;

  Attributes attr;
  Position pos;
  Point capture_point;
  tagElement() {
    memset(this, 0x00, sizeof(*this));
  }
  ~tagElement() {
  }
  void operator=(const tagElement &obj) {
    memcpy(this, &obj, sizeof(*this));
  }
  long X() const {
    return pos.left;
  }
  long Y() const {
    return pos.top;
  }
  long CX() const {
    return pos.right - pos.left;
  }
  long CY() const {
    return pos.bottom - pos.top;
  }
} Element;
#pragma pack(pop)

class IElement {
public:
  virtual void Release() const = 0;
};

class IAutomation : public Interface<IAutomation> {
public:
  using tfCaptureFinishedCb = void (*)(const unsigned long long &,
                                       const Element *);

public:
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
  virtual void RegisterCaptureFinishedCb(const unsigned long long &,
                                         const tfCaptureFinishedCb &) = 0;
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
  virtual bool Screenshot(const OverlayWindowType &) const = 0;
  virtual bool Start(const OverlayWindowType &) = 0;
  virtual void Stop() = 0;
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
  virtual void OnUioEvent(const uiohook::IUioEvent *) const = 0;
  virtual void SetPosition(const long &x, const long &y, const long &cx,
                           const long &cy) const = 0;
};
} // namespace inspect

#endif //__INSPECTION_H_