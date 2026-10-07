// 模块说明：快捷键实现，见 Hotkey.h
#include "stdafx.h"
#include "Hotkey.h"

// ---------------------------------------------------------------------------
// 键名表（第一个名字用于显示，其余为解析时接受的别名）
// ---------------------------------------------------------------------------
struct KeyName {
	BYTE vk;
	const wchar_t* name;
};

static const KeyName kKeyNames[] = {
	{ VK_NUMPAD0, L"Num0" }, { VK_NUMPAD1, L"Num1" }, { VK_NUMPAD2, L"Num2" }, { VK_NUMPAD3, L"Num3" },
	{ VK_NUMPAD4, L"Num4" }, { VK_NUMPAD5, L"Num5" }, { VK_NUMPAD6, L"Num6" }, { VK_NUMPAD7, L"Num7" },
	{ VK_NUMPAD8, L"Num8" }, { VK_NUMPAD9, L"Num9" },
	{ VK_MULTIPLY, L"NumMul" }, { VK_ADD, L"NumAdd" }, { VK_SUBTRACT, L"NumSub" }, { VK_DECIMAL, L"NumDot" }, { VK_DIVIDE, L"NumDiv" },
	{ VK_F1, L"F1" }, { VK_F2, L"F2" }, { VK_F3, L"F3" }, { VK_F4, L"F4" }, { VK_F5, L"F5" }, { VK_F6, L"F6" },
	{ VK_F7, L"F7" }, { VK_F8, L"F8" }, { VK_F9, L"F9" }, { VK_F10, L"F10" }, { VK_F11, L"F11" }, { VK_F12, L"F12" },
	{ VK_HOME, L"Home" }, { VK_END, L"End" }, { VK_INSERT, L"Insert" }, { VK_DELETE, L"Delete" },
	{ VK_PRIOR, L"PageUp" }, { VK_NEXT, L"PageDown" },
	{ VK_UP, L"Up" }, { VK_DOWN, L"Down" }, { VK_LEFT, L"Left" }, { VK_RIGHT, L"Right" },
	{ VK_SPACE, L"Space" }, { VK_TAB, L"Tab" }, { VK_RETURN, L"Enter" }, { VK_BACK, L"Backspace" },
	{ VK_ESCAPE, L"Esc" }, { VK_PAUSE, L"Pause" }, { VK_SCROLL, L"ScrollLock" }, { VK_CAPITAL, L"CapsLock" },
	{ VK_OEM_3, L"`" }, { VK_OEM_MINUS, L"-" }, { VK_OEM_PLUS, L"=" }, { VK_OEM_4, L"[" }, { VK_OEM_6, L"]" },
	{ VK_OEM_1, L";" }, { VK_OEM_7, L"'" }, { VK_OEM_COMMA, L"," }, { VK_OEM_PERIOD, L"." }, { VK_OEM_2, L"/" },
	{ VK_OEM_5, L"\\" },
	{ VK_XBUTTON1, L"Mouse4" }, { VK_XBUTTON2, L"Mouse5" }, { VK_MBUTTON, L"MouseMiddle" },
};

// 解析时额外接受的别名（统一转为小写后比较，空格已去掉）
static const KeyName kKeyAliases[] = {
	{ VK_DOWN, L"downarrow" }, { VK_UP, L"uparrow" }, { VK_LEFT, L"leftarrow" }, { VK_RIGHT, L"rightarrow" },
	{ VK_PRIOR, L"pgup" }, { VK_NEXT, L"pgdn" }, { VK_DELETE, L"del" }, { VK_INSERT, L"ins" },
	{ VK_RETURN, L"return" }, { VK_ESCAPE, L"escape" },
	{ VK_MULTIPLY, L"numeric*" }, { VK_MULTIPLY, L"num*" }, { VK_SUBTRACT, L"numeric-" }, { VK_SUBTRACT, L"num-" },
	{ VK_DIVIDE, L"numeric/" }, { VK_DIVIDE, L"num/" }, { VK_DECIMAL, L"numeric." }, { VK_DECIMAL, L"num." },
};

static void Lower(const wchar_t* in, wchar_t* out, int outSize) {
	int j = 0;
	for (int i = 0; in[i] && j < outSize - 1; ++i) {
		wchar_t c = in[i];
		if (c == L' ' || c == L'\t') continue;
		if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
		out[j++] = c;
	}
	out[j] = 0;
}

static int KeyFromName(const wchar_t* token) {
	wchar_t t[32];
	Lower(token, t, 32);
	if (!t[0]) return -1;
	// 原版写法 numeric1 -> num1
	if (wcsncmp(t, L"numeric", 7) == 0 && t[7] >= L'0' && t[7] <= L'9' && !t[8]) return VK_NUMPAD0 + (t[7] - L'0');
	if (wcsncmp(t, L"numpad", 6) == 0 && t[6] >= L'0' && t[6] <= L'9' && !t[7]) return VK_NUMPAD0 + (t[6] - L'0');
	// 单个字母 / 数字
	if (!t[1] && t[0] >= L'a' && t[0] <= L'z') return 'A' + (t[0] - L'a');
	if (!t[1] && t[0] >= L'0' && t[0] <= L'9') return '0' + (t[0] - L'0');
	for (size_t i = 0; i < sizeof(kKeyNames) / sizeof(kKeyNames[0]); ++i) {
		wchar_t n[32];
		Lower(kKeyNames[i].name, n, 32);
		if (wcscmp(n, t) == 0) return kKeyNames[i].vk;
	}
	for (size_t i = 0; i < sizeof(kKeyAliases) / sizeof(kKeyAliases[0]); ++i) {
		if (wcscmp(kKeyAliases[i].name, t) == 0) return kKeyAliases[i].vk;
	}
	return -1;
}

static void KeyToName(BYTE vk, wchar_t* out, int outSize) {
	if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
		_snwprintf(out, outSize, L"%c", (wchar_t)vk);
		return;
	}
	for (size_t i = 0; i < sizeof(kKeyNames) / sizeof(kKeyNames[0]); ++i) {
		if (kKeyNames[i].vk == vk) {
			lstrcpynW(out, kKeyNames[i].name, outSize);
			return;
		}
	}
	_snwprintf(out, outSize, L"Key%02X", vk);
}

bool Hotkey_Parse(const wchar_t* text, HotkeyBinding& out) {
	HotkeyBinding b = { 0, 0, { 0, 0, 0 } };
	wchar_t buf[128];
	lstrcpynW(buf, text ? text : L"", 128);
	// 只有空白：未设置
	bool blank = true;
	for (int i = 0; buf[i]; ++i) if (buf[i] != L' ' && buf[i] != L'\t') blank = false;
	if (blank) { out = b; return true; }

	// 按 '+' 切分（键名里不含 '+'，小键盘加号写作 NumAdd）
	wchar_t* tokens[8];
	int tokenCount = 0;
	wchar_t* start = buf;
	for (wchar_t* p = buf; ; ++p) {
		if (*p == L'+' || *p == 0) {
			bool end = (*p == 0);
			*p = 0;
			if (tokenCount < 8) tokens[tokenCount++] = start;
			if (end) break;
			start = p + 1;
		}
	}
	for (int i = 0; i < tokenCount; ++i) {
		wchar_t t[32];
		Lower(tokens[i], t, 32);
		if (!t[0]) continue;
		if (wcscmp(t, L"ctrl") == 0 || wcscmp(t, L"control") == 0) { b.mods |= HK_CTRL; continue; }
		if (wcscmp(t, L"alt") == 0) { b.mods |= HK_ALT; continue; }
		if (wcscmp(t, L"shift") == 0) { b.mods |= HK_SHIFT; continue; }
		int vk = KeyFromName(tokens[i]);
		if (vk < 0 || b.count >= 3) return false;
		b.keys[b.count++] = (BYTE)vk;
	}
	if (b.count == 0) return false;     // 只有修饰键不算有效快捷键
	out = b;
	return true;
}

void Hotkey_Format(const HotkeyBinding& binding, wchar_t* out, int outSize) {
	out[0] = 0;
	if (binding.count == 0) return;
	wchar_t text[96] = L"";
	if (binding.mods & HK_CTRL) wcscat(text, L"Ctrl+");
	if (binding.mods & HK_ALT) wcscat(text, L"Alt+");
	if (binding.mods & HK_SHIFT) wcscat(text, L"Shift+");
	for (int i = 0; i < binding.count && i < 3; ++i) {
		wchar_t name[24];
		KeyToName(binding.keys[i], name, 24);
		if (i > 0) wcscat(text, L"+");
		wcscat(text, name);
	}
	lstrcpynW(out, text, outSize);
}

LPARAM Hotkey_Pack(const HotkeyBinding& b) {
	DWORD v = (DWORD)(b.mods & 0x0F) | ((DWORD)(b.count & 0x0F) << 4);
	v |= (DWORD)b.keys[0] << 8;
	v |= (DWORD)b.keys[1] << 16;
	v |= (DWORD)b.keys[2] << 24;
	return (LPARAM)v;
}

HotkeyBinding Hotkey_Unpack(LPARAM packed) {
	DWORD v = (DWORD)packed;
	HotkeyBinding b;
	b.mods = (BYTE)(v & 0x0F);
	b.count = (BYTE)((v >> 4) & 0x0F);
	b.keys[0] = (BYTE)(v >> 8);
	b.keys[1] = (BYTE)(v >> 16);
	b.keys[2] = (BYTE)(v >> 24);
	if (b.count > 3) b.count = 3;
	return b;
}

bool Hotkey_Equal(const HotkeyBinding& a, const HotkeyBinding& b) {
	if (a.mods != b.mods || a.count != b.count) return false;
	for (int i = 0; i < a.count; ++i) if (a.keys[i] != b.keys[i]) return false;
	return true;
}

// ---------------------------------------------------------------------------
// 轮询线程
// ---------------------------------------------------------------------------
static CRITICAL_SECTION Lock;
static HANDLE Thread = NULL;
static volatile LONG Running = 0;
static HWND NotifyWindow = NULL;
static std::vector<HotkeyBinding> Bindings;
static std::vector<bool> WasActive;
static HotkeyFilter Filter = { NULL, 0, true, false };

// 捕获状态
static volatile LONG CaptureRequested = 0;
static bool Capturing = false;
static bool CaptureBaseline[256];        // 开始捕获时已经按着的键（卡住的键），松开之前一律忽略
static HotkeyBinding CaptureResult;

// 每个键最近一次“按下”的时刻：组合键只有在其中某个普通键是刚刚按下时才触发，
// 避免某个键状态卡住（例如按住小键盘键时切换了 NumLock）后，每按一次 Ctrl 都误触发
static bool KeyPrevDown[256];
static DWORD KeyPressTick[256];
static const DWORD kFreshPressMs = 600;

static inline bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static BYTE ModsDown() {
	BYTE m = 0;
	if (KeyDown(VK_CONTROL)) m |= HK_CTRL;
	if (KeyDown(VK_MENU)) m |= HK_ALT;
	if (KeyDown(VK_SHIFT)) m |= HK_SHIFT;
	return m;
}

static bool IsModifierVk(int vk) {
	return vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT ||
		(vk >= VK_LSHIFT && vk <= VK_RMENU) || vk == VK_LWIN || vk == VK_RWIN;
}

static bool IsCapturableVk(int vk) {
	if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_CANCEL) return false;
	if (IsModifierVk(vk)) return false;
	if (vk == VK_PROCESSKEY || vk == VK_PACKET || vk == 0xFF) return false;
	return true;
}

static bool ForegroundAllowed(const HotkeyFilter& f) {
	if (f.suppressed) return false;
	if (!f.onlyWhenActive) return true;
	HWND fg = GetForegroundWindow();
	if (!fg) return false;
	if (fg == f.mainWindow) return true;
	DWORD pid = 0;
	GetWindowThreadProcessId(fg, &pid);
	if (pid == GetCurrentProcessId()) return true;
	return f.gamePid && pid == f.gamePid;
}

static void CaptureStart() {
	Capturing = true;
	HotkeyBinding empty = { 0, 0, { 0, 0, 0 } };
	CaptureResult = empty;
	for (int vk = 0; vk < 256; ++vk) CaptureBaseline[vk] = vk > 0 && vk < 0xFF && KeyDown(vk);
}

// 捕获时的按键状态：忽略开始捕获时已经按着、且还没松开过的键
static bool CaptureKeyDown(int vk) {
	bool down = KeyDown(vk);
	if (!down) { CaptureBaseline[vk] = false; return false; }
	return !CaptureBaseline[vk];
}

static void CaptureStep() {
	bool anyDown = false;
	for (int vk = 1; vk < 0xFF; ++vk) {
		if (vk == VK_LBUTTON || vk == VK_RBUTTON) continue;
		if (CaptureKeyDown(vk)) anyDown = true;
	}
	if (anyDown) {
		if (CaptureKeyDown(VK_CONTROL)) CaptureResult.mods |= HK_CTRL;
		if (CaptureKeyDown(VK_MENU)) CaptureResult.mods |= HK_ALT;
		if (CaptureKeyDown(VK_SHIFT)) CaptureResult.mods |= HK_SHIFT;
		for (int vk = 1; vk < 0xFF; ++vk) {
			if (!IsCapturableVk(vk) || !CaptureKeyDown(vk)) continue;
			bool seen = false;
			for (int i = 0; i < CaptureResult.count; ++i) if (CaptureResult.keys[i] == vk) seen = true;
			if (!seen && CaptureResult.count < 3) CaptureResult.keys[CaptureResult.count++] = (BYTE)vk;
		}
		return;
	}
	// 全部松开：如果记录到了普通键就结束
	if (CaptureResult.count == 0) {
		CaptureResult.mods = 0;         // 只按了修饰键，继续等
		return;
	}
	Capturing = false;
	WPARAM kind = 1;
	if (CaptureResult.count == 1 && CaptureResult.mods == 0) {
		if (CaptureResult.keys[0] == VK_ESCAPE) kind = 0;
		else if (CaptureResult.keys[0] == VK_BACK || CaptureResult.keys[0] == VK_DELETE) kind = 2;
	}
	PostMessageW(NotifyWindow, WM_APP_HOTKEY_CAPTURED, kind, Hotkey_Pack(CaptureResult));
}

static DWORD WINAPI PollThread(LPVOID) {
	while (Running) {
		Sleep(20);
		DWORD now = GetTickCount();

		if (InterlockedExchange(&CaptureRequested, 0)) CaptureStart();

		EnterCriticalSection(&Lock);
		HotkeyFilter filter = Filter;
		bool capturing = Capturing;
		LeaveCriticalSection(&Lock);

		if (capturing) {
			CaptureStep();
			// 捕获期间所有组合都视为“按住”，避免捕获结束瞬间误触发
			EnterCriticalSection(&Lock);
			for (size_t i = 0; i < WasActive.size(); ++i) WasActive[i] = true;
			LeaveCriticalSection(&Lock);
			continue;
		}

		bool allowed = ForegroundAllowed(filter);
		BYTE mods = ModsDown();
		EnterCriticalSection(&Lock);
		// 记录各普通键的“刚按下”时刻（只检查快捷键里用到的键）
		bool keyDown[256];
		for (size_t i = 0; i < Bindings.size(); ++i) {
			for (int k = 0; k < Bindings[i].count; ++k) {
				BYTE vk = Bindings[i].keys[k];
				bool down = KeyDown(vk);
				if (down && !KeyPrevDown[vk]) KeyPressTick[vk] = now;
				KeyPrevDown[vk] = down;
				keyDown[vk] = down;
			}
		}
		for (size_t i = 0; i < Bindings.size(); ++i) {
			const HotkeyBinding& b = Bindings[i];
			bool active = b.count > 0 && b.mods == mods;
			bool fresh = false;
			for (int k = 0; active && k < b.count; ++k) {
				active = keyDown[b.keys[k]];
				if (now - KeyPressTick[b.keys[k]] <= kFreshPressMs) fresh = true;
			}
			if (active && !WasActive[i] && fresh && allowed) PostMessageW(NotifyWindow, WM_APP_HOTKEY, (WPARAM)i, 0);
			WasActive[i] = active;
		}
		LeaveCriticalSection(&Lock);
	}
	return 0;
}

void Hotkey_Start(HWND notifyWindow) {
	if (Thread) return;
	InitializeCriticalSection(&Lock);
	NotifyWindow = notifyWindow;
	Running = 1;
	Thread = CreateThread(NULL, 0, PollThread, NULL, 0, NULL);
}

void Hotkey_Stop() {
	if (!Thread) return;
	Running = 0;
	WaitForSingleObject(Thread, 1000);
	CloseHandle(Thread);
	Thread = NULL;
	DeleteCriticalSection(&Lock);
}

void Hotkey_SetBindings(const std::vector<HotkeyBinding>& bindings) {
	EnterCriticalSection(&Lock);
	Bindings = bindings;
	WasActive.assign(bindings.size(), true);    // 新设置的快捷键要先松开一次才会触发
	LeaveCriticalSection(&Lock);
}

void Hotkey_SetFilter(const HotkeyFilter& filter) {
	EnterCriticalSection(&Lock);
	Filter = filter;
	LeaveCriticalSection(&Lock);
}

void Hotkey_BeginCapture() {
	InterlockedExchange(&CaptureRequested, 1);
}

void Hotkey_CancelCapture() {
	InterlockedExchange(&CaptureRequested, 0);
	EnterCriticalSection(&Lock);
	Capturing = false;
	LeaveCriticalSection(&Lock);
}
