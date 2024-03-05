#include "stdafx.h"

Fix::Fix() {
}
Fix::~Fix() {
}
void Fix::Release() const {
  delete this;
}
bool Fix::Ready() const {
  return false;
}
Fix *__gpFix = nullptr;
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpFix) {
      break;
    }
    __gpFix = new Fix();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<IFix *>(__gpFix));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpFix) {
      break;
    }
    delete __gpFix;
    __gpFix = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif