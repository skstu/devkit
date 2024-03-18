#include "stdafx.h"

int main(int argc, char *argv[]) {
  std::string fmtstr = fmt::format("{}", 168);
  std::cout << fmtstr << std::endl;
  test_comp_zip();
  test_comp_zstd();
  test_comp_gzip();

  test_icu();
  test_ossl();

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