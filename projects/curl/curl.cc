#include "stdafx.h"

Curl::Curl() {
  Init();
}
Curl::~Curl() {
  UnInit();
}
void Curl::Release() const {
  delete this;
}
bool Curl::Ready() const {
  return false;
}
void Curl::Init() {
  CURLcode code;
  code = curl_global_init(CURL_GLOBAL_ALL);
  if (CURLE_OK == code) {
  }
}
void Curl::UnInit() {
  curl_global_cleanup();
}
Curl *__gpCurl = nullptr;
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpCurl) {
      break;
    }
    __gpCurl = new Curl();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<ICurl *>(__gpCurl));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpCurl) {
      break;
    }
    delete __gpCurl;
    __gpCurl = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif