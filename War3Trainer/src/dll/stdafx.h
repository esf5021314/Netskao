// 预编译头 / 公共包含
#ifndef STDAFX_H_INCLUDED_
#define STDAFX_H_INCLUDED_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601     // Windows 7（ChangeWindowMessageFilterEx 需要）
#endif

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fp_call.h>            // aero 通用函数指针调用（来自 TuringY Library/include）

#endif // STDAFX_H_INCLUDED_
