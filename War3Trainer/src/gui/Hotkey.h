// 模块说明：
// 快捷键的解析、格式化与后台轮询。
//
// 写法：修饰键（Ctrl / Alt / Shift）+ 最多 3 个普通键，用 + 连接，不区分大小写，例如
//     Ctrl+Q    Ctrl+Num1    Alt+E    Home    Down+Num1    Ctrl+Shift+K
// 兼容原版 CE 修改器的写法：“Ctrl+numeric 1”“Down Arrow+numeric 1”。
//
// 触发规则（与 CE 修改器相同，用 GetAsyncKeyState 轮询，不注册系统热键，不影响其它程序）：
//   * 普通键全部按下，且 Ctrl / Alt / Shift 的按下状态与设定完全一致时触发一次；
//   * 松开后才能再次触发。
//   * 小键盘数字键（Num0 ~ Num9）需要打开 NumLock。
#ifndef HOTKEY_H_INCLUDED_
#define HOTKEY_H_INCLUDED_

#include "stdafx.h"
#include <vector>

#define HK_CTRL  0x01
#define HK_ALT   0x02
#define HK_SHIFT 0x04

struct HotkeyBinding {
	BYTE mods;          // HK_CTRL | HK_ALT | HK_SHIFT
	BYTE count;         // 普通键个数，0 表示未设置
	BYTE keys[3];       // 虚拟键码
};

bool  Hotkey_Parse(const wchar_t* text, HotkeyBinding& out);         // 空字符串解析为“未设置”
void  Hotkey_Format(const HotkeyBinding& binding, wchar_t* out, int outSize);
LPARAM Hotkey_Pack(const HotkeyBinding& binding);
HotkeyBinding Hotkey_Unpack(LPARAM packed);
bool  Hotkey_Equal(const HotkeyBinding& a, const HotkeyBinding& b);

// 何时响应快捷键
struct HotkeyFilter {
	HWND  mainWindow;   // 修改器主窗口
	DWORD gamePid;      // 魔兽进程（0 = 未连接）
	bool  onlyWhenActive;   // true：只在游戏窗口或修改器窗口处于前台时响应
	bool  suppressed;   // true：暂停响应（正在编辑数值）
};

// 后台轮询线程
void Hotkey_Start(HWND notifyWindow);
void Hotkey_Stop();
void Hotkey_SetBindings(const std::vector<HotkeyBinding>& bindings);
void Hotkey_SetFilter(const HotkeyFilter& filter);
void Hotkey_BeginCapture();     // 下一次按下的组合键通过 WM_APP_HOTKEY_CAPTURED 返回
void Hotkey_CancelCapture();

#endif // HOTKEY_H_INCLUDED_
