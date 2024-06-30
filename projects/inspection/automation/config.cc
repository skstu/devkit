#include "stdafx.h"
Config::Config() {
  enable_focus_changed_event_.store(false);
  enable_mouse_clicked_event_.store(false);
  enable_mouse_move_event_.store(true);
  enable_capture_.store(false);
  enable_focus_changed_event_handler_.store(false);
}
Config::~Config() {
}
void Config::EnableFocusChangedEvent(const bool &enable) {
  std::lock_guard<std::mutex> lock{*mutex_};
  enable_focus_changed_event_.store(enable);
}
void Config::EnableMouseClickedEvent(const bool &enable) {
  std::lock_guard<std::mutex> lock{*mutex_};
  enable_mouse_clicked_event_.store(enable);
}
void Config::EnableMouseMoveEvent(const bool &enable) {
  std::lock_guard<std::mutex> lock{*mutex_};
  enable_mouse_move_event_.store(enable);
}
bool Config::EnableFocusChangedEvent() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return enable_focus_changed_event_.load();
}
bool Config::EnableMouseClickedEvent() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return enable_mouse_clicked_event_.load();
}
bool Config::EnableMouseMoveEvent() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return enable_mouse_move_event_.load();
}
void Config::EnableCapture(const bool &enable) {
  std::lock_guard<std::mutex> lock{*mutex_};
  enable_capture_.store(enable);
}
bool Config::EnableCaptureIs() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return enable_capture_;
}
void Config::EnableFocusChangedEventHandler(const bool &enable) {
  std::lock_guard<std::mutex> lock{*mutex_};
  enable_focus_changed_event_handler_.store(enable);
}
bool Config::EnableFocusChangedEventHandler() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return enable_focus_changed_event_handler_.load();
}