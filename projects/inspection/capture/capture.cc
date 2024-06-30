#include "stdafx.h"

Capture::Capture() {
  Init();
}
Capture::~Capture() {
  UnInit();
}
void Capture::Release() const {
  delete this;
}
void Capture::Init() {
}
void Capture::UnInit() {
}
bool Capture::Ready() const {
  return false;
}
bool Capture::Start() {
  return false;
}
void Capture::Stop() {
}
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
Capture *__gpCapture = nullptr;
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpCapture) {
      break;
    }
    __gpCapture = new Capture();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<ICapture *>(__gpCapture));
}
SHARED_API void interface_uninit() {
  SK_DELETE_PTR(__gpCapture);
}

#ifdef __cplusplus
}
#endif