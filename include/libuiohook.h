#ifndef __LIBUIOHOOK_H_
#define __LIBUIOHOOK_H_

#include "interface.h"

namespace uiohook {

enum class UioVirtualKeyCodes : uint16_t {
  ///////////////////////
  UIO_VC_A = 0x001E,
  UIO_VC_B = 0x0030,
  UIO_VC_C = 0x002E,
  UIO_VC_D = 0x0020,
  UIO_VC_E = 0x0012,
  UIO_VC_F = 0x0021,
  UIO_VC_G = 0x0022,
  UIO_VC_H = 0x0023,
  UIO_VC_I = 0x0017,
  UIO_VC_J = 0x0024,
  UIO_VC_K = 0x0025,
  UIO_VC_L = 0x0026,
  UIO_VC_M = 0x0032,
  UIO_VC_N = 0x0031,
  UIO_VC_O = 0x0018,
  UIO_VC_P = 0x0019,
  UIO_VC_Q = 0x0010,
  UIO_VC_R = 0x0013,
  UIO_VC_S = 0x001F,
  UIO_VC_T = 0x0014,
  UIO_VC_U = 0x0016,
  UIO_VC_V = 0x002F,
  UIO_VC_W = 0x0011,
  UIO_VC_X = 0x002D,
  UIO_VC_Y = 0x0015,
  UIO_VC_Z = 0x002C,
  ///////////////////////
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
enum class UioVirtualModifierMasks : uint16_t {
  UIO_MASK_SHIFT_L = 1 << 0,
  UIO_MASK_CTRL_L = 1 << 1,
  UIO_MASK_META_L = 1 << 2,
  UIO_MASK_ALT_L = 1 << 3,

  UIO_MASK_SHIFT_R = 1 << 4,
  UIO_MASK_CTRL_R = 1 << 5,
  UIO_MASK_META_R = 1 << 6,
  UIO_MASK_ALT_R = 1 << 7,

  UIO_MASK_SHIFT = UIO_MASK_SHIFT_L | UIO_MASK_SHIFT_R,
  UIO_MASK_CTRL = UIO_MASK_CTRL_L | UIO_MASK_CTRL_R,
  UIO_MASK_META = UIO_MASK_META_L | UIO_MASK_META_R,
  UIO_MASK_ALT = UIO_MASK_ALT_L | UIO_MASK_ALT_L,

  UIO_MASK_BUTTON1 = 1 << 8,
  UIO_MASK_BUTTON2 = 1 << 9,
  UIO_MASK_BUTTON3 = 1 << 10,
  UIO_MASK_BUTTON4 = 1 << 11,
  UIO_MASK_BUTTON5 = 1 << 12,

  UIO_MASK_NUM_LOCK = 1 << 13,
  UIO_MASK_CAPS_LOCK = 1 << 14,
  UIO_MASK_SCROLL_LOCK = 1 << 15,
};

enum class UioVirtualMouseButtons : uint16_t {
  UIO_MOUSE_BUTTON_ANY = 0,    // Any Button
  UIO_MOUSE_BUTTON_LEFT = 1,   // Left Button
  UIO_MOUSE_BUTTON_RIGHT = 2,  // Right Button
  UIO_MOUSE_BUTTON_MIDDLE = 3, // Middle Button
  UIO_MOUSE_BUTTON_EXTRA1 = 4, // Extra Mouse Button
  UIO_MOUSE_BUTTON_EXTRA2 = 5, // Extra Mouse Button

  UIO_MOUSE_BUTTON_TOTAL = 6,
  UIO_MOUSE_BUTTON_UNKNOWN = 0xFFFF,
};

enum class UioVirtualMouseWheel : int {
  UIO_WHEEL_UNIT_SCROLL = 1,
  UIO_WHEEL_BLOCK_SCROLL = 2,
  UIO_WHEEL_VERTICAL_DIRECTION = 3,
  UIO_WHEEL_HORIZONTAL_DIRECTION = 4,
};

enum class UioEventType : int {
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
  virtual void Free(void **) const = 0;
  virtual void Release() const = 0;
  virtual bool Ready() const = 0;
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual void MouseClick(const UioVirtualMouseButtons &, const long &x,
                          const long &y, const unsigned long &count) const = 0;
  virtual void MouseDragged(const long &to_x, const long &to_y,
                            const long &from_x, const long &from_y) const = 0;
  //!@ rotation == 1 (up)
  //!@ rotation == -1 (Then look down)
  virtual void MouseWheel(const long &x, const long &y, const long &amount,
                          const long &rotation) const = 0;
  virtual void MouseMoveTo(long x, long y, long step) const = 0;
  virtual void MouseSetTo(const long &x, const long &y) const = 0;

  virtual bool SetClipboardText(const char *, const long &) const = 0;
  virtual bool GetClipboardText(char **, long *) const = 0;
  virtual void UnregisterEvent(const unsigned long long & /*userdata ptr*/) = 0;
  virtual void RegisterEventCb(const unsigned long long &,
                               const tfEventCb &) = 0;
};
} // namespace uiohook

#endif //__LIBUIOHOOK_H_