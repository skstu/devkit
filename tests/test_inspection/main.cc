#include "stdafx.h"

inspect::Inspection *__gpInspect = nullptr;
uiohook::IUiohook *__gpUiohook = nullptr;
int main(int argc, char *argv[]) {
#ifdef _WIN32
  __gpInspect = inspect::Inspection::Create("inspection.dll");
  __gpUiohook = uiohook::IUiohook::Create("uiohook.dll");
#else
  __gpInspect = inspect::Inspection::Create("inspection.so");
#endif

  //__gpInspect->Start();
  __gpUiohook->Start();

  std::string input;
  do {
    input.clear();
    std::getline(std::cin, input);
    if (input == "q" || std::cin.eof()) {
      //__gpInspect->Stop();
      __gpUiohook->Stop();
      break;
    }
  } while (1);
  // inspect::Inspection::Destroy(&__gpInspect);
  uiohook::IUiohook::Destroy(&__gpUiohook);
  return 0;
}