#include <stl.h>
#include <win32.h>
int main(int argc, char* argv[]) {
#if defined(_DEBUG)
	::_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	//::_CrtSetBreakAlloc(3848);
#endif
	win::IWin* pWin = win::IWin::Create(R"(C:\Users\k34ub\source\skstu\devkit_last\bin\x64\Debug\win32.dll)");

	std::string md5src = "";
	char* buffer = nullptr;
	size_t buflen = 0;
	pWin->MD5(md5src.data(), md5src.size(), &buffer, &buflen);
	pWin->FreePtr((void**)&buffer);

	pWin->HASH_SHA1(md5src.data(), md5src.size(), &buffer, &buflen);
	pWin->FreePtr((void**)&buffer);


	pWin->HASH_SHA256(md5src.data(), md5src.size(), &buffer, &buflen);
	pWin->FreePtr((void**)&buffer);

	std::string input;
	do {
		input.clear();
		std::getline(std::cin, input);
		if (input == "q" || std::cin.eof()) {

			break;
		}
	} while (1);

	win::IWin::Destroy(&pWin);
	return 0;
}