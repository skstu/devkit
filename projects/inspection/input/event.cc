#include "stdafx.h"
UioEvent::UioEvent(const uiohook_event &ev) {
  memset(&keypressed, static_cast<uint16_t>(UioVirtualKeyCodes::UIO_VC_UNKNOWN),
         sizeof(keypressed));
  memset(
      &mousesbuttonpressed,
      static_cast<uint16_t>(UioVirtualMouseButtons::UIO_MOUSE_BUTTON_UNKNOWN),
      sizeof(mousesbuttonpressed));
  memcpy(&uiohook_event_, &ev, sizeof(uiohook_event));
  uiokeyboardeventdata_ = new UioKeyboardEventData(ev);
  uiomouseeventdata_ = new UioMouseEventData(ev);
  uiomousewheeleventdata_ = new UioMouseWheelEventData(ev);
}
UioEvent::~UioEvent() {
  uiomouseeventdata_->Release();
  uiokeyboardeventdata_->Release();
  uiomousewheeleventdata_->Release();
}
UioEventType UioEvent::Type() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return static_cast<UioEventType>(uiohook_event_.type);
}
uint64_t UioEvent::Time() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return uiohook_event_.time;
}
uint16_t UioEvent::Mask() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return uiohook_event_.mask;
}
uint16_t UioEvent::Reserved() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return uiohook_event_.reserved;
}
unsigned long long UioEvent::GetUserData() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return userdata_;
}
void UioEvent::SetUserData(const unsigned long long &data) {
  std::lock_guard<std::mutex> lock{*mutex_};
  userdata_ = data;
}
void UioEvent::Release() const {
  delete this;
}
IUioMouseWheelEventData *UioEvent::DataWheel() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return dynamic_cast<IUioMouseWheelEventData *>(uiomousewheeleventdata_);
}
IUioMouseEventData *UioEvent::DataMouse() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return dynamic_cast<IUioMouseEventData *>(uiomouseeventdata_);
}
IUioKeyboardEventData *UioEvent::DataKeyboard() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return dynamic_cast<IUioKeyboardEventData *>(uiokeyboardeventdata_);
}
IUioScreenData *UioEvent::DataScreen() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return nullptr;
}
void UioEvent::SetMousePressed(
    const std::map<UioVirtualMouseButtons, bool> &map_) {
  std::lock_guard<std::mutex> lock{*mutex_};
  memset(
      &mousesbuttonpressed,
      static_cast<uint16_t>(UioVirtualMouseButtons::UIO_MOUSE_BUTTON_UNKNOWN),
      sizeof(mousesbuttonpressed));
  size_t pos = 0;
  for (const auto &it : map_) {
    if (it.second == true)
      mousesbuttonpressed[pos++] = it.first;
  }
}
void UioEvent::SetKeyPressed(const std::map<UioVirtualKeyCodes, bool> &map_) {
  std::lock_guard<std::mutex> lock{*mutex_};
  memset(&keypressed, static_cast<uint16_t>(UioVirtualKeyCodes::UIO_VC_UNKNOWN),
         sizeof(keypressed));
  size_t pos = 0;
  for (const auto &it : map_) {
    if (it.second == true) {
      keypressed[pos++] = it.first;
    }
  }
}
bool UioEvent::IsKeyPressed(const UioVirtualKeyCodes &key) const {
  bool result = false;
  std::lock_guard<std::mutex> lock{*mutex_};
  for (size_t i = 0; i < sizeof(keypressed) / sizeof(keypressed[0]); ++i) {
    if (keypressed[i] == key) {
      result = true;
      break;
    } else if (keypressed[i] == UioVirtualKeyCodes::UIO_VC_UNKNOWN) {
      break;
    }
  }
  return result;
}
bool UioEvent::IsMousePressed(const UioVirtualMouseButtons &btn) const {
  bool result = false;
  std::lock_guard<std::mutex> lock{*mutex_};
  for (size_t i = 0;
       i < sizeof(mousesbuttonpressed) / sizeof(mousesbuttonpressed[0]); ++i) {
    if (mousesbuttonpressed[i] == btn) {
      result = true;
      break;
    } else if (mousesbuttonpressed[i] ==
               UioVirtualMouseButtons::UIO_MOUSE_BUTTON_UNKNOWN) {
      break;
    }
  }
  return result;
}