#include "stdafx.h"

BrowserFingerprint::BrowserFingerprint() {
}

BrowserFingerprint::~BrowserFingerprint() {
}
void BrowserFingerprint::Release() const {
  delete this;
}
bool BrowserFingerprint::Location() {
  return false;
}
bool BrowserFingerprint::IP_address() {
  return false;
}
bool BrowserFingerprint::WebRTC_address() {
  return false;
}
bool BrowserFingerprint::IP_location() {
  return false;
}
bool BrowserFingerprint::Timezone_from_Javascript() {
  return false;
}
bool BrowserFingerprint::Time_from_Javascript() {
  return false;
}
bool BrowserFingerprint::Time_from_IP() {
  return false;
}
bool BrowserFingerprint::Daylight_savings_time() {
  return false;
}
bool BrowserFingerprint::Languages_from_Javascript() {
  return false;
}
bool BrowserFingerprint::Accept_Language_header() {
  return false;
}
bool BrowserFingerprint::Internationalization_API() {
  return false;
}
bool BrowserFingerprint::User_Agent_HTTP() {
  return false;
}
bool BrowserFingerprint::User_Agent_Javascript() {
  return false;
}
bool BrowserFingerprint::Platform() {
  return false;
}
bool BrowserFingerprint::DoNotTrack() {
  return false;
}
bool BrowserFingerprint::HardwareConcurency() {
  return false;
}
bool BrowserFingerprint::Screen_resolution() {
  return false;
}
bool BrowserFingerprint::Available_screen_size() {
  return false;
}
bool BrowserFingerprint::Canvas_hash() {
  return false;
}
bool BrowserFingerprint::WebGL_hash() {
  return false;
}
bool BrowserFingerprint::WebGL_unmasked_vendor() {
  return false;
}
bool BrowserFingerprint::WebGL_unmasked_renderer() {
  return false;
}
bool BrowserFingerprint::WebGL_version() {
  return false;
}
bool BrowserFingerprint::AudioContext_hash() {
  return false;
}
bool BrowserFingerprint::Font_hash() {
  return false;
}
bool BrowserFingerprint::Fonts() {
  return false;
}

void BrowserFingerprint::OutputJson(char **, size_t *) const {
  return;
}
bool BrowserFingerprint::InputJson(const char *, size_t) {
  return false;
}