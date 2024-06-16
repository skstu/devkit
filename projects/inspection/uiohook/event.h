#ifndef __PROJECTS_UIOHOOK_EVENT_H_
#define __PROJECTS_UIOHOOK_EVENT_H_

class UioScreenData final : public IUioScreenData {
public:
  UioScreenData(const screen_data &sd) {
    number = sd.number;
    x = sd.x;
    y = sd.y;
    width = sd.width;
    height = sd.height;
  }
  void Release() const {
    delete this;
  }

private:
  virtual ~UioScreenData() {
  }

protected:
  uint8_t Number() const override final {
    return number;
  }
  int16_t X() const override final {
    return x;
  }
  int16_t Y() const override final {
    return y;
  }
  uint16_t Width() const override final {
    return width;
  }
  uint16_t Height() const override final {
    return height;
  }

private:
  uint8_t number;
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;
};

class UioKeyboardEventData final : public IUioKeyboardEventData {
public:
  UioKeyboardEventData(const uiohook_event &ev) {
    keycode = ev.data.keyboard.keycode;
    rawcode = ev.data.keyboard.rawcode;
    keychar = ev.data.keyboard.keychar;
  }
  void Release() const {
    delete this;
  }

private:
  virtual ~UioKeyboardEventData() {
  }

protected:
  uint16_t Keycode() const override final {
    return keycode;
  }
  uint16_t Rawcode() const override final {
    return rawcode;
  }
  uint16_t Keychar() const override final {
    return keychar;
  }

private:
  uint16_t keycode;
  uint16_t rawcode;
  uint16_t keychar;
};

class UioMouseEventData final : public IUioMouseEventData {
public:
  UioMouseEventData(const uiohook_event &ev) {
    button = ev.data.mouse.button;
    clicks = ev.data.mouse.clicks;
    x = ev.data.mouse.x;
    y = ev.data.mouse.y;
  }
  void Release() const {
    delete this;
  }

private:
  virtual ~UioMouseEventData() {
  }

protected:
  uint16_t Button() const override final {
    return button;
  }
  uint16_t Clicks() const override final {
    return clicks;
  }
  int16_t X() const override final {
    return x;
  }
  int16_t Y() const override final {
    return y;
  }

private:
  uint16_t button;
  uint16_t clicks;
  int16_t x;
  int16_t y;
};

class UioMouseWheelEventData final : public IUioMouseWheelEventData {
public:
  UioMouseWheelEventData(const uiohook_event &ev) {
    clicks = ev.data.wheel.clicks;
    x = ev.data.wheel.x;
    y = ev.data.wheel.y;
    type = ev.data.wheel.type;
    amount = ev.data.wheel.amount;
    rotation = ev.data.wheel.rotation;
    direction = ev.data.wheel.direction;
  }
  void Release() const {
    delete this;
  }

private:
  virtual ~UioMouseWheelEventData() {
  }

protected:
  uint16_t Clicks() const override final {
    return clicks;
  }
  int16_t X() const override final {
    return x;
  }
  int16_t Y() const override final {
    return y;
  }
  uint8_t Type() const override final {
    return type;
  }
  uint16_t Amount() const override final {
    return amount;
  }
  int16_t Rotation() const override final {
    return rotation;
  }
  uint8_t Direction() const override final {
    return direction;
  }

private:
  uint16_t clicks;
  int16_t x;
  int16_t y;
  uint8_t type;
  uint16_t amount;
  int16_t rotation;
  uint8_t direction;
};

class UioEvent final : public IUioEvent {
public:
  UioEvent(const uiohook_event &);

private:
  virtual ~UioEvent();

protected:
  UioEventType Type() const override final;
  uint64_t Time() const override final;
  uint16_t Mask() const override final;
  uint16_t Reserved() const override final;
  IUioMouseWheelEventData *DataWheel() const override final;
  IUioMouseEventData *DataMouse() const override final;
  IUioKeyboardEventData *DataKeyboard() const override final;
  IUioScreenData *DataScreen() const override final;
  bool IsMousePressed(const UioVirtualMouseButtons &btn) const override final;
  bool IsKeyPressed(const UioVirtualKeyCodes &key) const override final;
  unsigned long long GetUserData() const override final;

public:
  void Release() const;
  void SetUserData(const unsigned long long &);
  void SetMousePressed(const std::map<UioVirtualMouseButtons, bool> &);
  void SetKeyPressed(const std::map<UioVirtualKeyCodes, bool> &);

private:
  UioKeyboardEventData *uiokeyboardeventdata_ = nullptr;
  UioMouseEventData *uiomouseeventdata_ = nullptr;
  UioMouseWheelEventData *uiomousewheeleventdata_ = nullptr;
  uiohook_event uiohook_event_;
  unsigned long long userdata_ = 0;
  UioVirtualKeyCodes
      keypressed[static_cast<uint16_t>(UioVirtualKeyCodes::UIO_VC_TOTAL)];
  UioVirtualMouseButtons mousesbuttonpressed[static_cast<uint16_t>(
      UioVirtualMouseButtons::UIO_MOUSE_BUTTON_TOTAL)];
};

#endif //__PROJECTS_UIOHOOK_EVENT_H_