#include "stdafx.h"

int main(int argc, char *argv[]) {
  std::string fmtstr = fmt::format("{}", 168);
  std::cout << fmtstr << std::endl;
  test_comp_zip();
  test_comp_zstd();
  test_comp_gzip();

  test_icu();
  return 0;
}