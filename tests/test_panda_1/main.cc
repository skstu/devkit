#include "stdafx.h"
panda::IPanda *__gpPanda = nullptr;
int main(int argc, char *argv[]) {
#ifdef _WIN32
  __gpPanda = panda::IPanda::Create("panda.dll");
#else
  __gpPanda = panda::IPanda::Create("panda.so");
#endif

  if (__gpPanda) {
    __gpPanda->Test();
  }
  std::string input;
  do {
    input.clear();
    std::getline(std::cin, input);
    if (input == "q" || std::cin.eof()) {
      break;
    }
  } while (1);
  panda::IPanda::Destroy(&__gpPanda);
  return 0;
}