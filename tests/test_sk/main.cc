#include <iostream>
#include <sk.h>

int main(int argc, char *argv[]) {
  char *zip_compress_data = nullptr;
  size_t zip_compress_data_len = 0;
  sk_zip_compress("", 0, &zip_compress_data, &zip_compress_data_len);
  std::cout << "Hello SK !" << std::endl;

  return 0;
}