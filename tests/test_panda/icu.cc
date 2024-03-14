#include "stdafx.h"

void test_icu() {
  const char *text = "你好啊 在吗在吗？";
  char *name = nullptr;
  sk_icu_detect_name(text, strlen(text), &name);
  sk_icu_conv_type type;
  sk_icu_detect_type(text, strlen(text), &type);
  sk_mem_free((void **)&name);
  int sk = 0;
}