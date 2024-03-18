#include "stdafx.h"

Panda::Panda() {
}

Panda::~Panda() {
}

bool Panda::Ready() const {
  return true;
}
void Panda::Release() const {
  delete this;
}
IBrowserFingerprint *Panda::CreateBF() const {
  return nullptr;
}
void Panda::Test() const {
#ifdef _WIN32
  ::MessageBoxA(NULL, "Hello!", "I'm Martell!", MB_TOPMOST);
#else

#endif
}
Panda *__gpPanda = nullptr;
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpPanda) {
      break;
    }
    __gpPanda = new Panda();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<IPanda *>(__gpPanda));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpPanda) {
      break;
    }
    delete __gpPanda;
    __gpPanda = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif