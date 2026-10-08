// 模块说明：
// 模拟的魔兽进程（测试用，只支持 1.24E）：创建窗口类名为 "Warcraft III" 的窗口，
// 以 DONT_RESOLVE_DLL_REFERENCES 方式映射真实的 1.24E Game.dll，然后跑消息循环。
//   war3.exe <1.24E Game.dll 路径>
//
// 模拟“游戏每帧”：把 Game.dll+0x39CC45（CWorldFrameWar3 每帧函数里 call 挂钩点之后的那条指令）
// 改成 ret，然后从 Game.dll+0x39CC40 那条真实的 call 指令开始执行 —— 挂钩点被调用时的返回地址
// 与真实游戏完全相同，修改器的每帧挂钩就在本进程主线程里运行。除这两条指令外不执行 Game.dll 的任何代码。
//
// 测试专用消息（发给窗口）：
//   WM_APP+77：调用 Game.dll+0xAC90（读取 Misc 常量）读取 "MaxHeroLevel"，
//              只在“英雄最大等级”挂钩已打开时发送 —— 存根会在进入游戏代码之前直接返回设定值；
//   WM_APP+78：wParam = 1 开始每 15ms 一帧，0 停止（模拟进入 / 离开对局画面）。
#include <windows.h>

static HMODULE Game = NULL;
static const DWORD kFrameCallRva = 0x39CC40;      // call 0x4D3E30（每帧挂钩点）
static const DWORD kFrameReturnRva = 0x39CC45;    // 上面这条 call 的返回地址

#define TIMER_EXIT  1
#define TIMER_FRAME 2

static bool FramePrepare() {
	BYTE* ret = reinterpret_cast<BYTE*>(Game) + kFrameReturnRva;
	DWORD old;
	if (!VirtualProtect(ret, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
	*ret = 0xC3;
	VirtualProtect(ret, 1, old, &old);
	FlushInstructionCache(GetCurrentProcess(), ret, 1);
	return true;
}

static void FrameRun() {
	// 0x39CC40: call 挂钩点（ecx = 写入全局变量的值，真实游戏里是一个对象指针，这里给非 0 即可）
	typedef void (__fastcall *FrameCallFn)(int value);
	FrameCallFn fn = reinterpret_cast<FrameCallFn>(reinterpret_cast<BYTE*>(Game) + kFrameCallRva);
	fn(1);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
	if (m == WM_APP + 77 && Game) {
		typedef int (__fastcall *GetMiscIntFn)(const char* section, const char* key, int def);
		BYTE* base = reinterpret_cast<BYTE*>(Game);
		GetMiscIntFn fn = reinterpret_cast<GetMiscIntFn>(base + 0xAC90);
		return fn(reinterpret_cast<const char*>(base + 0x887EF0), reinterpret_cast<const char*>(base + 0x888368), 7);
	}
	if (m == WM_APP + 78) {
		if (w) SetTimer(h, TIMER_FRAME, 15, NULL);
		else KillTimer(h, TIMER_FRAME);
		return 1;
	}
	if (m == WM_TIMER && w == TIMER_FRAME) {
		FrameRun();
		return 0;
	}
	if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR cmdLine, int) {
	if (!cmdLine || !cmdLine[0]) {
		MessageBoxW(NULL, L"用法：war3.exe <1.24E Game.dll 路径>", L"MockGame", MB_OK);
		return 1;
	}
	char path[MAX_PATH];
	lstrcpynA(path, cmdLine, MAX_PATH);
	if (path[0] == '"') { lstrcpynA(path, cmdLine + 1, MAX_PATH); char* q = path; while (*q && *q != '"') ++q; *q = 0; }
	Game = LoadLibraryExA(path, NULL, DONT_RESOLVE_DLL_REFERENCES);
	if (!Game || !FramePrepare()) return 2;

	WNDCLASSA wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.lpszClassName = "Warcraft III";
	RegisterClassA(&wc);
	HWND hwnd = CreateWindowA("Warcraft III", "Warcraft III", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, NULL, NULL, inst, NULL);
	ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	SetTimer(hwnd, TIMER_EXIT, 120000, NULL);      // 120 秒后自动退出
	MSG msg;
	while (GetMessageA(&msg, NULL, 0, 0) > 0) {
		if (msg.message == WM_TIMER && msg.wParam == TIMER_EXIT && msg.hwnd == hwnd) break;
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}
	return 0;
}
