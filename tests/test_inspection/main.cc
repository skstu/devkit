#include "stdafx.h"

int main(int argc, char *argv[]) {

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