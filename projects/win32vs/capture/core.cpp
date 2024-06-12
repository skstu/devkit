#include "stdafx.h"

Core::Core(const HMODULE& _m) : module_(_m) {
 Init();
}

Core::~Core() {
 UnInit();
}
bool Core::Ready() const {
 return ready_.load();
}
void Core::Init() {
 path_python_ = Global::GetModulePath(GetModuleHandleW(NULL));
 path_ = Global::GetModulePath(module_);
 LOG_INIT(path_python_ + "\\__CapturePyd.log");
 do {
#ifdef _DEBUG
  pInspection_ = inspect::Inspection::Create(R"(C:\Users\k34ub\source\skstu\devkit\out\inspection.dll)");
#else
  pInspection_ = inspect::Inspection::Create((path_python_ + R"(\inspection.dll)").c_str());
#endif
  if (!pInspection_)
   break;
  ready_.store(true);
 } while (0);
}

void Core::UnInit() {
 do {
  if (!ready_.load())
   break;
  inspect::Inspection::Destroy(&pInspection_);
  ready_.store(false);
 } while (0);
 LOG_UNINIT;
}





Core* __gpCore = nullptr;
Core* CoreGet() { return __gpCore; }
