#include "stdafx.h"
UioEvent::UioEvent(const uiohook_event &ev) {
  memset(&keypressed, UioVirtualKeyCodes::UIO_VC_UNKNOWN, sizeof(keypressed));
  memset(&mousesbuttonpressed, UioVirtualMouseButtons::UIO_MOUSE_BUTTON_UNKNOWN,
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
  return static_cast<UioEventType>(uiohook_event_.type);
}
uint64_t UioEvent::Time() const {
  return uiohook_event_.time;
}
uint16_t UioEvent::Mask() const {
  return uiohook_event_.mask;
}
uint16_t UioEvent::Reserved() const {
  return uiohook_event_.reserved;
}
unsigned long long UioEvent::GetUserData() const {
  return userdata_;
}
void UioEvent::SetUserData(const unsigned long long &data) {
  userdata_ = data;
}
void UioEvent::Release() const {
  delete this;
}
IUioMouseWheelEventData *UioEvent::DataWheel() const {
  return dynamic_cast<IUioMouseWheelEventData *>(uiomousewheeleventdata_);
}
IUioMouseEventData *UioEvent::DataMouse() const {
  return dynamic_cast<IUioMouseEventData *>(uiomouseeventdata_);
}
IUioKeyboardEventData *UioEvent::DataKeyboard() const {
  return dynamic_cast<IUioKeyboardEventData *>(uiokeyboardeventdata_);
}
IUioScreenData *UioEvent::DataScreen() const {
  return nullptr;
}
void UioEvent::SetMousePressed(
    const std::map<UioVirtualMouseButtons, bool> &map_) {
  memset(&mousesbuttonpressed, UioVirtualMouseButtons::UIO_MOUSE_BUTTON_UNKNOWN,
         sizeof(mousesbuttonpressed));
  size_t pos = 0;
  for (const auto &it : map_) {
    if (it.second == true)
      mousesbuttonpressed[pos++] = it.first;
  }
}
void UioEvent::SetKeyPressed(const std::map<UioVirtualKeyCodes, bool> &map_) {
  memset(&keypressed, UioVirtualKeyCodes::UIO_VC_UNKNOWN, sizeof(keypressed));
  size_t pos = 0;
  for (const auto &it : map_) {
    if (it.second == true) {
      keypressed[pos++] = it.first;
    }
  }
}
bool UioEvent::IsKeyPressed(const UioVirtualKeyCodes &key) const {
  bool result = false;
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