#include "stdafx.h"
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd) {
  std::string ss("sdafasdf");
  Tools *pTools = new Tools();
  LOG_INIT(pTools->CurrentProcessPath() + "\\perform.log");
  bool is_exit = false;
  do {
    if (lpCmdLine == nullptr) {
      break;
    }
    std::string strCmdLine(lpCmdLine);
    std::vector<std::string> args = stl::String::StringSpilt(strCmdLine, " ");
    for (const auto &arg : args) {
      if (arg == "") {
        continue;
      }
      if (arg == "--get-processes") {
        pTools->GetTaskbarProcesses();
        is_exit = true;
      }
    }
    // lpCmdLine
  } while (false);

  inspect::Inspection *insp = nullptr;
  do {
    if (is_exit) {

      break;
    }
    insp = inspect::Inspection::Create(R"(inspection.dll)");
    if (insp == nullptr) {
      break;
    }
    if (!insp->Start()) {
      break;
    }
    while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    insp->Stop();
  } while (false);
  if (insp != nullptr) {
    insp->Stop();
    inspect::Inspection::Destroy(&insp);
  }

  pTools->Release();
  LOG_UNINIT;
  return 0;
}
