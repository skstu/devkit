#include "stdafx.h"
wxui::IWxui *__gpWxui = nullptr;
int main(int argc, char *argv[]) {
#ifdef _WIN32
  __gpWxui = wxui::IWxui::Create("wxui.dll");
#else
  __gpWxui = wxui::IWxui::Create("wxui.so");
#endif
  if (__gpWxui) {
    __gpWxui->UICreate();
  }
  std::string input;
  do {
    input.clear();
    std::getline(std::cin, input);
    if (input == "q" || std::cin.eof()) {
      if (__gpWxui) {
        __gpWxui->UIDestroy();
      }
      break;
    }
  } while (1);
  wxui::IWxui::Destroy(&__gpWxui);
  return 0;
}