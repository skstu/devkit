#include "stdafx.h"

curl::ICurl *__gpCurl = nullptr;
int main(int argc, char *argv[]) {
#ifdef _WIN32
  __gpCurl = curl::ICurl::Create("curl.dll");
#else
  __gpCurl = curl::ICurl::Create("curl.so");
#endif

  std::string input;
  do {
    input.clear();
    std::getline(std::cin, input);
    if (input == "q" || std::cin.eof()) {
      break;
    }
  } while (1);
  curl::ICurl::Destroy(&__gpCurl);
  return 0;
}