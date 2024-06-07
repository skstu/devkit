#include "stdafx.h"

Opencv::Opencv() {
  Init();
}
Opencv::~Opencv() {
  UnInit();
}
void Opencv::Release() const {
  delete this;
}
bool Opencv::Ready() const {
  return false;
}
void Opencv::Init() {

  std::cout << cv::getBuildInformation().c_str() << std::endl;
}
void Opencv::UnInit() {
}
bool Opencv::Start() {
  do {
    if (open_.load())
      break;
    open_.store(true);
    threads_.emplace_back([this]() { Perform(); });
  } while (0);
  return open_.load();
}
void Opencv::Stop() {
  do {
    if (!open_.load())
      break;

    open_.store(false);
    for (auto &t : threads_)
      t.join();
    threads_.clear();
  } while (0);
}
void Opencv::Perform() {
}
Opencv *__gpOpencv = nullptr;
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpOpencv) {
      break;
    }
    __gpOpencv = new Opencv();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<IOpencv *>(__gpOpencv));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpOpencv) {
      break;
    }
    delete __gpOpencv;
    __gpOpencv = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif