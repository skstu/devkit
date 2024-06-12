#include "test.h"

void test_pyd() {
 using tf_op_init = unsigned long long(*)(void);
 using tf_op_release = void(*)(unsigned long long);
 do {
  auto module_ = LoadLibraryA(R"(C:\Users\k34ub\source\skstu\devkit\bin\win\vs\build\x64\Debug\capture.dll)");
  auto err = GetLastError();
  if (!module_)
   break;
  tf_op_init op_init = (tf_op_init)GetProcAddress(module_, "op_init");
  tf_op_release op_release = (tf_op_release)GetProcAddress(module_, "op_release");
  if (!op_init || !op_release)
   break;
  auto obj = op_init();


  op_release(obj);
  auto sk = 0;
 } while (0);
}