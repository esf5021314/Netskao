// 模块说明：
// 模拟的魔兽进程：创建窗口类名为 "Warcraft III" 的窗口并加载 Game.dll，然后跑消息循环。
//   MockGame.exe                 加载同目录下模拟的 Game.dll（只有版本信息）
//   MockGame.exe <Game.dll路径>  以 DONT_RESOLVE_DLL_REFERENCES 方式映射真实的 Game.dll
//                                （不执行其中任何代码），用于在真实二进制上测试补丁引擎
// 测试专用消息 WM_APP+77：调用 Game.dll+0xAC90（1.24E 读取 Misc 常量）读取 "MaxHeroLevel"，
// 只在“英雄最大等级”挂钩已打开时发送 —— 存根会在进入游戏代码之前直接返回设定值。
#include <windows.h>

static HMODULE Game = NULL;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
	if (m == WM_APP + 77 && Game) {
		typedef int (__fastcall *GetMiscIntFn)(const char* section, const char* key, int def);
		BYTE* base = reinterpret_cast<BYTE*>(Game);
		GetMiscIntFn fn = reinterpret_cast<GetMiscIntFn>(base + 0xAC90);
		return fn(reinterpret_cast<const char*>(base + 0x887EF0), reinterpret_cast<const char*>(base + 0x888368), 7);
	}
	if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR cmdLine, int) {
	WNDCLASSA wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.lpszClassName = "Warcraft III";
	RegisterClassA(&wc);
	HWND hwnd = CreateWindowA("Warcraft III", "Warcraft III", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, NULL, NULL, inst, NULL);
	ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	if (cmdLine && cmdLine[0]) {
		char path[MAX_PATH];
		lstrcpynA(path, cmdLine, MAX_PATH);
		if (path[0] == '"') { lstrcpynA(path, cmdLine + 1, MAX_PATH); char* q = path; while (*q && *q != '"') ++q; *q = 0; }
		Game = LoadLibraryExA(path, NULL, DONT_RESOLVE_DLL_REFERENCES);
	} else {
		Game = LoadLibraryA("Game.dll");
	}
	if (!Game) return 2;
	SetTimer(hwnd, 1, 90000, NULL);      // 90 秒后自动退出
	MSG msg;
	while (GetMessageA(&msg, NULL, 0, 0) > 0) {
		if (msg.message == WM_TIMER && msg.wParam == 1 && msg.hwnd == hwnd) break;
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}
	return 0;
}
