#include "test.h"


void test_win32() {


 std::string input;
 do {
  input.clear();
  std::getline(std::cin, input);
  if (input == "q" || std::cin.eof()) {
   break;
  }
 } while (1);
}
