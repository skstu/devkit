#include "test.h"

int main(int argc, char* argv[]) {
#if TEST_PYD
	test_pyd();
#endif

#if TEST_WIN32
	test_win32();
#endif


#if TEST_CV
	test_cv();
#endif
	return 0;
}
