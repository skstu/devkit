#include "test.h"

void test_pyd() {
	using tf_op_init = unsigned long long(*)(void);
	using tf_op_release = void(*)(unsigned long long);
	do {
		std::string env_path = getenv("PATH");
		env_path.append(";");
		env_path.append(R"(C:\Users\k34ub\source\skstu\devkit\bin\win\vs\build\x64\Debug\)");
		if (0 != putenv(fmt::format("PATH={}", env_path).c_str()))
			break;
		auto module_ = LoadLibraryA(R"(capture.dll)");
		if (!module_)
			break;
#if 0
		tf_op_init op_init = (tf_op_init)GetProcAddress(module_, "op_init");
		tf_op_release op_release = (tf_op_release)GetProcAddress(module_, "op_release");
		if (!op_init || !op_release)
			break;
		auto obj = op_init();
		op_release(obj);
#endif
		FreeLibrary(module_);
		auto sk = 0;
	} while (0);
}