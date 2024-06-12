#include "stdafx.h"

inspect::IOverlay *__gpIOverlay = nullptr;
int main(int argc, char *argv[]) {
#ifdef _WIN32
  __gpIOverlay = inspect::IOverlay::Create("overlay.dll");
#else
  __gpIOverlay = inspect::IOverlay::Create("overlay.so");
#endif

  __gpIOverlay->Start();

  std::string input;
  do {
    input.clear();
    std::getline(std::cin, input);
    if (input == "q" || std::cin.eof()) {
      __gpIOverlay->Stop();
      break;
    }
  } while (1);
  inspect::IOverlay::Destroy(&__gpIOverlay);
  return 0;
}