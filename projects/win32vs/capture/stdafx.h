#ifndef CAPTURE_AFX_H
#define CAPTURE_AFX_H
#include <macros.h>
#include <stl.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib,"Shlwapi.lib")
#include <wchar.h>
#include <log.hpp>

#include <../projects/inspection/include/inspection.h>



#define PY_SSIZE_T_CLEAN
#include <pybind11/pybind11.h>

#include "types.h"
#include "global.h"
#include "core.h"
#include "pyd.h"



#pragma comment(lib,"dl.lib")
#pragma comment(lib,"stl.lib")
#if _DEBUG
#pragma comment(lib,"fmtd.lib")
#else
#pragma comment(lib,"fmt.lib")
#endif
#endif //CAPTURE_AFX_H
