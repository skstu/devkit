#include "stdafx.h"

Config::Config() {
  label_ = L"Undefined";
  enable_draw_border_.store(true);
}

Config::~Config() {
}
void Config::Release() const {
  delete this;
}
void Config::SetLabel(const wchar_t *label) {
  std::lock_guard<std::mutex> lock{*mutex_};
  if (label)
    label_ = label;
}
std::wstring Config::GetLabel() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return label_;
}
void Config::EnableScreenshot(const bool &enable) {
  std::lock_guard<std::mutex> lock{*mutex_};
  enable_screenshot_.store(enable);
}
bool Config::EnableScreenshot() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return enable_screenshot_.load();
}
void Config::EnableDrawBorder(const bool &enable) {
  std::lock_guard<std::mutex> lock{*mutex_};
  enable_draw_border_.store(enable);
}
void Config::SetBorderARGB(const unsigned short &a, const unsigned short &r,
                           const unsigned short &g, const unsigned short &b) {
  std::lock_guard<std::mutex> lock{*mutex_};
  border_color_a = a;
  border_color_r = r;
  border_color_g = g;
  border_color_b = b;
}
bool Config::EnableDrawBorder() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return enable_draw_border_.load();
}
unsigned short Config::BorderColorA() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return border_color_a;
}
unsigned short Config::BorderColorR() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return border_color_r;
}
unsigned short Config::BorderColorG() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return border_color_g;
}
unsigned short Config::BorderColorB() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return border_color_b;
}