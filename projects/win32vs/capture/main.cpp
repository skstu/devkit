#include "stdafx.h"

static int capture_library_shutdown(void);
static int capture_library_startup(void);

#if ENABLE_WIN32_DLLMAIN
BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
 switch (ul_reason_for_call) {
 case DLL_PROCESS_ATTACH: {
	 __gpHinstance = hModule;
	 __gpCore = new Core(reinterpret_cast<HMODULE>(__gpHinstance));

  capture_library_startup();
 }break;
 case DLL_THREAD_ATTACH: {

 }break;
 case DLL_THREAD_DETACH: {

 }break;
 case DLL_PROCESS_DETACH: {
  capture_library_shutdown();
 }break;
 }
 return TRUE;
}
#endif

#if !ENABLE_WIN32_DLLMAIN
#if defined(__GNUC__) && !defined(_WIN32)
__attribute__((destructor))
#endif
#endif
int capture_library_shutdown(void)
{
 static int was_shutdown = 0;
 do {
  if (was_shutdown == 1)
   break;

  was_shutdown = 1;
 } while (0);
 return 0;
}

#if !ENABLE_WIN32_DLLMAIN
#if defined(__GNUC__) && !defined(_WIN32)
__attribute__((constructor))
#endif
#endif
int capture_library_startup(void)
{
 static int was_startup;
 do {
  if (was_startup == 1)
   break;

  was_startup = 1;
 } while (0);
 return 0;
}

