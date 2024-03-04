#include "stdafx.h"
void test_comp_zip() {
  const char *test_data = "Hello SK !";
  char *comp = nullptr;
  size_t compLen = 0;
  sk_comp_zip(test_data, strlen(test_data), &comp, &compLen);

  char *uncomp = nullptr;
  size_t uncompLen = strlen(test_data);
  sk_comp_unzip(comp, compLen, &uncomp, &uncompLen);

  std::cout << __func__ << ":" << std::string(uncomp, uncompLen) << std::endl;
  sk_mem_free((void **)&comp);
  sk_mem_free((void **)&uncomp);
}
void test_comp_gzip() {
  const char *test_data = "Hello SK !";
  char *comp = nullptr;
  size_t compLen = 0;
  sk_comp_gzip(test_data, strlen(test_data), &comp, &compLen);

  char *uncomp = nullptr;
  size_t uncompLen = 0;
  sk_comp_ungzip(comp, compLen, &uncomp, &uncompLen);
  std::cout << __func__ << ":" << std::string(uncomp, uncompLen) << std::endl;

  sk_mem_free((void **)&comp);
  sk_mem_free((void **)&uncomp);
}

void test_comp_zstd() {
  const char *test_data = "Hello SK !";
  char *comp = nullptr;
  size_t compLen = 0;
  sk_comp_zstd(test_data, strlen(test_data), &comp, &compLen);

  char *uncomp = nullptr;
  size_t uncompLen = 0;
  sk_comp_unzstd(comp, compLen, &uncomp, &uncompLen);
  std::cout << __func__ << ":" << std::string(uncomp, uncompLen) << std::endl;

  sk_mem_free((void **)&comp);
  sk_mem_free((void **)&uncomp);
}