#ifndef _INTERFACE_H__
#define _INTERFACE_H__

#include <dlfcn.h>
#include "shared_library.h"

using tf_interface_init = void* (*)(void*, unsigned long);
using tf_interface_uninit = void (*)(void);
const char name_interface_init[] = "interface_init";
const char name_interface_uninit[] = "interface_uninit";
template <typename T>
class Interface {
 public:
  static T* Create(const char* dyPathname) {
    T* result = nullptr;
    void* handle_ = nullptr;
    tf_interface_init init_ = nullptr;
    tf_interface_uninit uninit_ = nullptr;
    do {
      if (!dyPathname) {
        break;
      }
      handle_ = dlopen(dyPathname, /*RTLD_NOW*/ RTLD_LAZY);
      if (!handle_) {
        break;
      }
      init_ = decltype(init_)(dlsym(handle_, name_interface_init));
      uninit_ = decltype(uninit_)(dlsym(handle_, name_interface_uninit));
      if (!init_ || !uninit_) {
        break;
      }
      result = (T*)init_(nullptr, 0);
      if (!result) {
        uninit_();
        break;
      }
      result->handle_ = handle_;
      result->interface_init_ = init_;
      result->interface_uninit_ = uninit_;
    } while (0);
    if (!result && handle_) {
      dlclose(handle_);
      handle_ = nullptr;
    }
    return result;
  }
  static void Destroy(T** obj) {
    do {
      if (!(*obj)) {
        break;
      }
      auto _this = dynamic_cast<Interface*>(*obj);
      if (!_this) {
        break;
      }
      auto htmp = _this->handle_;
      if (_this->interface_uninit_) {
        _this->interface_uninit_();
      }
      if (htmp) {
        dlclose(htmp);
      }
      *obj = nullptr;
    } while (0);
  }

 private:
  tf_interface_init interface_init_ = nullptr;
  tf_interface_uninit interface_uninit_ = nullptr;
  void* handle_ = nullptr;
};

#endif  // _INTERFACE_H__
