#include "stdafx.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
 switch (ul_reason_for_call) {
 case DLL_PROCESS_ATTACH: {
  SK_DELETE_PTR(__gpCore);
  __gpCore = new Core(hModule);
  if (!__gpCore->Ready()) {
   SK_DELETE_PTR(__gpCore);
   break;
  }
 }break;
 case DLL_THREAD_ATTACH: {

 }break;
 case DLL_THREAD_DETACH: {

 }break;
 case DLL_PROCESS_DETACH: {
  SK_DELETE_PTR(__gpCore);
 }break;
 }
 return TRUE;
}

