#include "stdafx.h"

int main(int argc, char *argv[]) {
  int x1 = 1;     // canvas
  int x2 = 2;     // webgl
  int x3 = 4;     // audio
  int x4 = 8;     // font
  int x5 = 16;    // clientrects
  int x6 = 32;    // webrtc
  int x7 = 64;    // navigator
  int x8 = 128;   // timezone
  int x9 = 256;   // logs
  int x10 = 512;  // random
  int x11 = 1024; // no-cache
  int x12 = 2048; // wlist

  int x = x1 | x2 | x3 | x4 | x10 | x11;

  test_uv();

  std::string test_str = "aadbbdklalk#$%^asdflkasdghas";

#if 0
  std::string fmtstr = fmt::format("{}", 168);
  std::cout << fmtstr << std::endl;
  test_comp_zip();
  test_comp_zstd();
  test_comp_gzip();

  test_icu();
  test_ossl();
#endif
  std::string input;
  do {
    input.clear();
    std::getline(std::cin, input);
    if (input == "q" || std::cin.eof()) {
      break;
    }
  } while (1);
  return 0;
}