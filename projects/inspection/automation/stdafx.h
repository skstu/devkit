#ifndef __PROJECTS_INSPECTION_AUTOMATION_AFX_H_
#define __PROJECTS_INSPECTION_AUTOMATION_AFX_H_
#include <macros.h>
#include <stl.h>
#include <fmt/format.h>
#include <ossl.h>
#include <inspection.h>
using namespace inspect;
#include <win32.h>
using namespace win;

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <comdef.h>
#include <comutil.h>
#include <gdiplus.h>
#include <UIAutomation.h>
#include <ShellScalingApi.h>

#include "config.h"
#include "element.h"
#include "element_array.h"
#include "automation.h"
#endif ///__PROJECTS_INSPECTION_AUTOMATION_AFX_H_