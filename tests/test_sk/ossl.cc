#include "stdafx.h"

void test_ossl() {
  char *__md5 = NULL;
  size_t __md5Len = 0;
  sk_ossl_md5("", 0, &__md5, &__md5Len);
  std::cout << "MD5:"
            << stl::String::BinaryToHexString(std::string(__md5, __md5Len))
            << std::endl;
  int __break = 0;
  sk_mem_free((void **)&__md5);
}