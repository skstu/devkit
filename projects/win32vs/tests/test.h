#ifndef TEST_H
#define TEST_H

#define TEST_PYD 1
#define TEST_WIN32 0
#define TEST_CV 0


#include <cvpp.h>
#include <stl.h>
#include <windows.h>
#include <fmt/format.h>




#pragma comment(lib,"dl.lib")
#if _DEBUG
#pragma comment(lib,"fmtd.lib")
#else
#pragma comment(lib,"fmt.lib")
#endif
#pragma comment(lib,"stl.lib")

#if TEST_PYD
void test_pyd();
#endif
#if TEST_CV
void test_cv();
#endif
#if TEST_WIN32
void test_win32();
#endif

#endif