#ifndef __LIBUIOHOOK_H_
#define __LIBUIOHOOK_H_

#include "interface.h"

namespace uiohook {

enum UioVirtualKeyCodes {
  UIO_VC_ESCAPE = 0x0001,
  UIO_VC_SHIFT_L = 0x002A,
  UIO_VC_SHIFT_R = 0x0036,
  UIO_VC_CONTROL_L = 0x001D,
  UIO_VC_CONTROL_R = 0x0E1D,
  UIO_VC_ALT_L = 0x0038,
  UIO_VC_ALT_R = 0x0E38,

  UIO_VC_TOTAL = 7,
  UIO_VC_UNKNOWN = 0xFFFF,
};

enum UioVirtualMouseButtons {
  UIO_MOUSE_BUTTON_ANY = 0,    // Any Button
  UIO_MOUSE_BUTTON_LEFT = 1,   // Left Button
  UIO_MOUSE_BUTTON_RIGHT = 2,  // Right Button
  UIO_MOUSE_BUTTON_MIDDLE = 3, // Middle Button
  UIO_MOUSE_BUTTON_EXTRA1 = 4, // Extra Mouse Button
  UIO_MOUSE_BUTTON_EXTRA2 = 5, // Extra Mouse Button

  UIO_MOUSE_BUTTON_TOTAL = 6,
  UIO_MOUSE_BUTTON_UNKNOWN = 0xFFFF,
};

enum UioVirtualMouseWheel {
  UIO_WHEEL_UNIT_SCROLL = 1,
  UIO_WHEEL_BLOCK_SCROLL = 2,
  UIO_WHEEL_VERTICAL_DIRECTION = 3,
  UIO_WHEEL_HORIZONTAL_DIRECTION = 4,
};

enum UioEventType {
  UIO_EVENT_HOOK_ENABLED = 1,
  UIO_EVENT_HOOK_DISABLED,
  UIO_EVENT_KEY_TYPED,
  UIO_EVENT_KEY_PRESSED,
  UIO_EVENT_KEY_RELEASED,
  UIO_EVENT_MOUSE_CLICKED,
  UIO_EVENT_MOUSE_PRESSED,
  UIO_EVENT_MOUSE_RELEASED,
  UIO_EVENT_MOUSE_MOVED,
  UIO_EVENT_MOUSE_DRAGGED,
  UIO_EVENT_MOUSE_WHEEL
};

class IUioScreenData {
public:
  virtual uint8_t Number() const = 0;
  virtual int16_t X() const = 0;
  virtual int16_t Y() const = 0;
  virtual uint16_t Width() const = 0;
  virtual uint16_t Height() const = 0;
};
class IUioKeyboardEventData {
public:
  virtual uint16_t Keycode() const = 0;
  virtual uint16_t Rawcode() const = 0;
  virtual uint16_t Keychar() const = 0;
};
class IUioMouseEventData {
public:
  virtual uint16_t Button() const = 0;
  virtual uint16_t Clicks() const = 0;
  virtual int16_t X() const = 0;
  virtual int16_t Y() const = 0;
};
class IUioMouseWheelEventData {
public:
  virtual uint16_t Clicks() const = 0;
  virtual int16_t X() const = 0;
  virtual int16_t Y() const = 0;
  virtual uint8_t Type() const = 0;
  virtual uint16_t Amount() const = 0;
  virtual int16_t Rotation() const = 0;
  virtual uint8_t Direction() const = 0;
};

class IUioEvent {
public:
  virtual UioEventType Type() const = 0;
  virtual uint64_t Time() const = 0;
  virtual uint16_t Mask() const = 0;
  virtual uint16_t Reserved() const = 0;
  virtual IUioMouseWheelEventData *DataWheel() const = 0;
  virtual IUioMouseEventData *DataMouse() const = 0;
  virtual IUioKeyboardEventData *DataKeyboard() const = 0;
  virtual IUioScreenData *DataScreen() const = 0;
  virtual bool IsKeyPressed(const UioVirtualKeyCodes &key) const = 0;
  virtual bool IsMousePressed(const UioVirtualMouseButtons &btn) const = 0;
  virtual unsigned long long GetUserData() const = 0;
};

class IUiohook : public Interface<IUiohook> {
public:
  using tfEventCb = void (*)(const IUioEvent *);

public:
  virtual void Release() const = 0;
  virtual bool Ready() const = 0;
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual void UnregisterEvent(const unsigned long long & /*userdata ptr*/) = 0;
  virtual void RegisterEventCb(const unsigned long long &,
                               const tfEventCb &) = 0;
};
} // namespace uiohook

#endif //__LIBUIOHOOK_H_