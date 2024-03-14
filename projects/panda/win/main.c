#include "stdafx.h"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, // handle to DLL module
                    DWORD fdwReason,    // reason for calling function
                    LPVOID lpvReserved) // reserved
{
  // Perform actions based on the reason for calling.
  switch (fdwReason) {
  case DLL_PROCESS_ATTACH:
    // Initialize once for each new process.
    // Return FALSE to fail DLL load.
    sk_library_startup();
    break;

  case DLL_THREAD_ATTACH:
    // Do thread-specific initialization.
    break;

  case DLL_THREAD_DETACH:
    // Do thread-specific cleanup.
    break;

  case DLL_PROCESS_DETACH:

    if (lpvReserved != NULL) {
      break; // do not do cleanup if process termination scenario
    }

    // Perform any necessary cleanup.
    sk_library_shutdown();
    break;
  }
  return TRUE; // Successful DLL_PROCESS_ATTACH.
}