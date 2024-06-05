#include "stdafx.h"

inspect::Inspection *__gpInspect = nullptr;
int main(int argc, char *argv[]) {
#ifdef _WIN32
  __gpInspect = inspect::Inspection::Create("inspection.dll");
#else
  __gpInspect = inspect::Inspection::Create("inspection.so");
#endif

  __gpInspect->Start();

  std::string input;
  do {
    input.clear();
    std::getline(std::cin, input);
    if (input == "q" || std::cin.eof()) {
      __gpInspect->Stop();
      break;
    }
  } while (1);
  inspect::Inspection::Destroy(&__gpInspect);
  return 0;
}