// 预编译头 / 公共包含（界面程序）
#ifndef GUI_STDAFX_H_INCLUDED_
#define GUI_STDAFX_H_INCLUDED_

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601     // Windows 7
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif

#include <windows.h>
#include <commctrl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../common/Shared.h"

// 界面线程之间通信用的自定义消息
#define WM_APP_HOTKEY           (WM_APP + 1)    // wParam = 行号（快捷键触发）
#define WM_APP_HOTKEY_CAPTURED  (WM_APP + 2)    // wParam = 1 完成 / 0 取消 / 2 清除，lParam = 打包后的快捷键

#endif // GUI_STDAFX_H_INCLUDED_
