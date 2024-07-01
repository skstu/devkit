#include <stl.h>
#include <libuiohook.h>
int main(int argc, char* argv[]) {
#if defined(_DEBUG)
	::_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	//::_CrtSetBreakAlloc(3848);
#endif
	uiohook::IUiohook* pUiohook = uiohook::IUiohook::Create(
		R"(C:\Users\k34ub\source\skstu\devkit_last\bin\x64\Debug\input.dll)");

	if (!pUiohook->Start())
		return -1;
	std::string input;
	do {
		input.clear();
		std::getline(std::cin, input);
		if (input == "q" || std::cin.eof()) {
			pUiohook->Stop();
			break;
		}
	} while (1);

	uiohook::IUiohook::Destroy(&pUiohook);
	return 0;
}