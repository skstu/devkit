#include "stdafx.h"
#include <quickfix.h>
fix::IFix *__gpFix = nullptr;
int main(int argc, char *argv[]) {
#ifdef _WIN32
  __gpFix = fix::IFix::Create("fix.dll");
#else
  __gpFix = fix::IFix::Create("fix.so");
#endif

  std::string input;
  do {
    input.clear();
    std::getline(std::cin, input);
    if (input == "q" || std::cin.eof()) {
      break;
    }
  } while (1);
  fix::IFix::Destroy(&__gpFix);
  return 0;
}