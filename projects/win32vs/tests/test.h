#ifndef TEST_H
#define TEST_H

#include <cvpp.h>
#include <stl.h>
#include <windows.h>

#pragma comment(lib,"dl.lib")
#if _DEBUG
#pragma comment(lib,"fmtd.lib")
#else
#pragma comment(lib,"fmt.lib")
#endif
#pragma comment(lib,"stl.lib")

//void test_cv();
//void test_pyd();
void test_win32();


#endif