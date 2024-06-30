#ifndef CAPTURE_AFX_H
#define CAPTURE_AFX_H

#define PYD_VERSION "24.625.2055.0"

#define ENABLE_LOG 0
#define ENABLE_WIN32_DLLMAIN 1
//!@ mode 1: FindElementFormRoot
//!@ mode 2: FindElementFromElement
#define ENABLE_FIND_ELEMENT_MODE 1

#include <macros.h>
#include <stl.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ShlDisp.h>
#include <shlwapi.h>
#pragma comment(lib,"Shlwapi.lib")
#include <wchar.h>
#include <log.hpp>
#if !ENABLE_WIN32_DLLMAIN
#include <dlfcn.h>
#endif

#include <../projects/inspection/include/inspection.h>
#include <win.h>
#include <libuiohook.h>


#define PY_SSIZE_T_CLEAN
#include <pybind11/pybind11.h>

#include "global.h"
#include "object.h"
#include "core.h"
#include "pyd.h"

#pragma comment(lib,"dl.lib")
#pragma comment(lib,"stl.lib")
#ifdef _DEBUG
#pragma comment(lib,"fmtd.lib")
#else
#pragma comment(lib,"fmt.lib")
#endif
#endif //CAPTURE_AFX_H
