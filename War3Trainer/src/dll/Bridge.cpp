// 模块说明：
// 注入模块与界面程序之间的桥：
//   1. 初始化线程：等待 Game.dll → 识别版本 → 初始化偏移表 → 创建共享内存 → 子类化魔兽窗口；
//   2. 窗口过程（运行在游戏主线程）：收到界面投递的命令消息后执行；
//      第一次收到消息时 SetTimer，之后每 100ms 刷新状态；
//   3. 卸载：还原补丁与窗口过程，然后 FreeLibraryAndExitThread。
//
// 为什么用窗口消息而不是原版的“Hook 每帧函数”：
//   魔兽的游戏逻辑与消息循环在同一个主线程里，窗口过程里调用 JASS 原生函数与在
//   每帧函数里调用是同一个线程、同一个时机；而窗口子类化不需要任何版本相关的地址，
//   换版本不用重新找 Hook 点，也不需要改写游戏代码。
#include "stdafx.h"
#include "Bridge.h"
#include "Offsets.h"
#include "Jass.h"
#include "Patch.h"
#include "Trainer.h"
#include "SafeCall.h"
#include "../common/Shared.h"

#define W3T_TIMER_ID 0x5733     // 'W3'

static HMODULE SelfModule = NULL;
static HWND GameHwnd = NULL;
static WNDPROC OriginalWndProc = NULL;
static UINT CommandMessage = 0;
static HANDLE SharedHandle = NULL;
static W3T_Shared* Shared = NULL;
static LONG ProcessedSeq = 0;
static bool TimerStarted = false;
static bool Detached = false;

// ---------------------------------------------------------------------------
// 版本识别
// ---------------------------------------------------------------------------
struct VersionName {
	DWORD build;
	const wchar_t* name;
	DWORD support;
};

// 支持程度：1.24E 全部功能已逐条反汇编核对；1.20E 补丁数据来自原版脚本；其它版本暂时只有原生函数表
// （补丁 / 物品表 / 内部函数的支持位在初始化时按偏移表实际登记情况计算，这里只标记“已核对”）
static const VersionName kVersionNames[] = {
	{ 6048,  L"1.20A", 0 },
	{ 6074,  L"1.20E", 0 },
	{ 6263,  L"1.21",  0 },
	{ 6300,  L"1.21B", 0 },
	{ 6328,  L"1.22",  0 },
	{ 6352,  L"1.23",  0 },
	{ 6372,  L"1.24A", 0 },
	{ 6374,  L"1.24B", 0 },
	{ 6378,  L"1.24C", 0 },
	{ 6384,  L"1.24D", 0 },
	{ 6387,  L"1.24E", W3T_SUPPORT_VERIFIED },
	{ 6397,  L"1.25",  0 },
	{ 6401,  L"1.26",  0 },
	{ 52240, L"1.27A", 0 },
	{ 7085,  L"1.27B", 0 },
	{ 7205,  L"1.28A", 0 },
	{ 7395,  L"1.28",  0 },
	{ 7680,  L"1.28F", 0 },
};

static DWORD ModuleBuildGet(HMODULE module) {
	wchar_t path[MAX_PATH];
	if (!GetModuleFileNameW(module, path, MAX_PATH)) return 0;
	DWORD dummy = 0;
	DWORD size = GetFileVersionInfoSizeW(path, &dummy);
	if (!size) return 0;
	BYTE* data = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, size));
	if (!data) return 0;
	DWORD build = 0;
	VS_FIXEDFILEINFO* info = NULL;
	UINT infoSize = 0;
	if (GetFileVersionInfoW(path, 0, size, data) &&
		VerQueryValueW(data, L"\\", reinterpret_cast<void**>(&info), &infoSize) && info) {
		build = info->dwFileVersionLS & 0xFFFF;     // 1.24.4.6387 -> 6387，1.27.0.52240 -> 52240
	}
	HeapFree(GetProcessHeap(), 0, data);
	return build;
}

// ---------------------------------------------------------------------------
// 窗口
// ---------------------------------------------------------------------------
struct FindWindowContext {
	DWORD pid;
	HWND hwnd;
};

static BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM lParam) {
	FindWindowContext* ctx = reinterpret_cast<FindWindowContext*>(lParam);
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid != ctx->pid) return TRUE;
	char cls[64];
	if (GetClassNameA(hwnd, cls, sizeof(cls)) && lstrcmpiA(cls, "Warcraft III") == 0) {
		ctx->hwnd = hwnd;
		return FALSE;
	}
	return TRUE;
}

static HWND GameWindowFind() {
	FindWindowContext ctx = { GetCurrentProcessId(), NULL };
	EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&ctx));
	return ctx.hwnd;
}

// 允许低权限进程向本窗口投递命令消息（Vista 起的 UIPI），失败不影响功能
static void MessageFilterAllow(HWND hwnd, UINT message) {
	typedef BOOL(WINAPI* ChangeWindowMessageFilterExFn)(HWND, UINT, DWORD, void*);
	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	ChangeWindowMessageFilterExFn fn = user32 ?
		reinterpret_cast<ChangeWindowMessageFilterExFn>(reinterpret_cast<void*>(GetProcAddress(user32, "ChangeWindowMessageFilterEx"))) : NULL;
	if (fn) fn(hwnd, message, 1 /* MSGFLT_ALLOW */, NULL);
}

// ---------------------------------------------------------------------------
// 卸载
// ---------------------------------------------------------------------------
static DWORD WINAPI UnloadThread(LPVOID) {
	Sleep(500);     // 等主线程从窗口过程返回
	FreeLibraryAndExitThread(SelfModule, 0);
	return 0;
}

static void BridgeUnload() {
	if (Detached) return;
	if (TimerStarted) KillTimer(GameHwnd, W3T_TIMER_ID);
	TimerStarted = false;
	Trainer_Shutdown(Shared);

	// 只有窗口过程仍然是我们的时候才能还原；否则说明其它插件在我们之后又子类化了窗口，
	// 这时卸载会破坏它的调用链，只能留在内存里做透传。
	bool canFree = false;
	if (OriginalWndProc && GameHwnd &&
		reinterpret_cast<WNDPROC>(GetWindowLongPtrA(GameHwnd, GWLP_WNDPROC)) == BridgeWndProc) {
		SetWindowLongPtrA(GameHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OriginalWndProc));
		canFree = true;
	}
	Detached = true;
	if (Shared) {
		InterlockedExchange(&Shared->dllState, DLL_UNLOADED);
		UnmapViewOfFile(Shared);
		Shared = NULL;
	}
	if (SharedHandle) {
		CloseHandle(SharedHandle);
		SharedHandle = NULL;
	}
	if (canFree) {
		HANDLE thread = CreateThread(NULL, 0, UnloadThread, NULL, 0, NULL);
		if (thread) CloseHandle(thread);
	}
}

// ---------------------------------------------------------------------------
// 窗口过程（游戏主线程）
// ---------------------------------------------------------------------------
static void CommandsDrain() {
	// 依次处理 ProcessedSeq+1 .. cmdSeq；环形缓冲 32 格，超出的旧命令丢弃
	LONG last = Shared->cmdSeq;
	if (last - ProcessedSeq > W3T_CMD_RING) ProcessedSeq = last - W3T_CMD_RING;
	while (ProcessedSeq < last) {
		LONG seq = ++ProcessedSeq;
		W3T_CmdSlot slot = Shared->ring[seq % W3T_CMD_RING];
		if (slot.seq != seq) continue;  // 槽位尚未写完或已被覆盖
		Trainer_Execute(Shared, slot);
	}
}

LRESULT CALLBACK BridgeWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (!Detached && Shared) {
		if (message == CommandMessage && CommandMessage) {
			if (!TimerStarted) TimerStarted = SetTimer(hwnd, W3T_TIMER_ID, 100, NULL) != 0;
			switch (wParam) {
			case W3T_WP_COMMAND:
				CommandsDrain();
				break;
			case W3T_WP_SYNC_TOGGLES:
				Trainer_SyncToggles(Shared);
				break;
			case W3T_WP_UNLOAD:
				BridgeUnload();
				break;
			default:
				break;
			}
			return 0;
		}
		if (message == WM_TIMER && wParam == W3T_TIMER_ID) {
			InterlockedIncrement(&Shared->heartbeat);
			Trainer_Tick(Shared);
			return 0;
		}
	}
	return CallWindowProcA(OriginalWndProc, hwnd, message, wParam, lParam);
}

// ---------------------------------------------------------------------------
// 初始化线程
// ---------------------------------------------------------------------------
static void InitFail(const wchar_t* text) {
	if (!Shared) return;
	lstrcpynW(Shared->initError, text, 128);
	InterlockedExchange(&Shared->dllState, DLL_FAILED);
}

static DWORD WINAPI InitThread(LPVOID) {
	// 1. 共享内存（由注入模块创建，权限与游戏进程一致；界面程序随后打开）
	wchar_t name[64];
	_snwprintf(name, 63, W3T_SHM_NAME_FMT, GetCurrentProcessId());
	name[63] = 0;
	SharedHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(W3T_Shared), name);
	if (!SharedHandle) return 1;
	bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
	Shared = static_cast<W3T_Shared*>(MapViewOfFile(SharedHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(W3T_Shared)));
	if (!Shared) return 1;
	if (!existed || Shared->magic != W3T_SHARED_MAGIC) {
		ZeroMemory(Shared, sizeof(W3T_Shared));
		Shared->inGameMessages = 1;
	}
	Shared->magic = W3T_SHARED_MAGIC;
	Shared->abiVersion = W3T_ABI_VERSION;
	Shared->structSize = sizeof(W3T_Shared);
	for (int i = 0; i < W3T_TOGGLE_SLOTS; ++i) {
		Shared->toggleState[i] = 0;
		Shared->toggleWant[i] = 0;
	}
	ProcessedSeq = Shared->cmdSeq;      // 忽略上一次会话遗留的命令
	InterlockedExchange(&Shared->dllState, DLL_STARTING);

	// 2. 等待 Game.dll（从启动器注入时游戏可能还没加载完）
	HMODULE game = NULL;
	for (int i = 0; i < 600 && !game; ++i) {
		game = GetModuleHandleW(L"Game.dll");
		if (!game) Sleep(100);
	}
	if (!game) { InitFail(L"等待 Game.dll 超时，这不是魔兽争霸 III 进程？"); return 1; }

	// 3. 版本识别与偏移表
	DWORD build = ModuleBuildGet(game);
	Offset_Init(build, reinterpret_cast<DWORD>(game));
	Shared->gameBuild = build;
	Shared->gameBase = reinterpret_cast<DWORD>(game);
	const VersionName* version = NULL;
	for (size_t i = 0; i < sizeof(kVersionNames) / sizeof(kVersionNames[0]); ++i) {
		if (kVersionNames[i].build == build) version = &kVersionNames[i];
	}
	if (version) lstrcpynW(Shared->versionName, version->name, 16);
	else _snwprintf(Shared->versionName, 15, L"未知(%lu)", build);

	DWORD support = version ? version->support : 0;
	int missing = 0;
	if (jass::NativesComplete(&missing)) support |= W3T_SUPPORT_NATIVES;
	if (Offset(GLOBAL_GAMEUI)) support |= W3T_SUPPORT_MOUSE;
	if (PatchSupported(TGL_NO_DEFEAT) || PatchSupported(TGL_FUN_MODE) || PatchSupported(TGL_AURA_STACK)) support |= W3T_SUPPORT_PATCHES;
	if (PatchSupported(TGL_MAX_HERO_LEVEL)) support |= W3T_SUPPORT_MAXLEVEL;
	if (Offset(GLOBAL_ITEMDATA_TABLE)) support |= W3T_SUPPORT_ITEMLIST;
	if (Offset(UNIT_ADD_ABILITY_INTERNAL)) support |= W3T_SUPPORT_ABILINT;
	Shared->supportFlags = support;
	DWORD toggles = 1u << TGL_NOCD_NOMANA;          // 由定时器维持，只依赖原生函数
	for (int id = 0; id < TGL_COUNT; ++id) {
		if (id != TGL_NOCD_NOMANA && PatchSupported(id)) toggles |= 1u << id;
	}
	Shared->toggleSupport = toggles;
	if (!(support & W3T_SUPPORT_NATIVES)) {
		wchar_t text[128];
		_snwprintf(text, 127, L"不支持的游戏版本 %ls（build %lu），函数表缺少编号 %d", Shared->versionName, build, missing);
		text[127] = 0;
		InitFail(text);
		return 1;
	}

	// 4. 找到魔兽主窗口并子类化（窗口可能比 Game.dll 晚创建）
	HWND hwnd = NULL;
	for (int i = 0; i < 600 && !hwnd; ++i) {
		hwnd = GameWindowFind();
		if (!hwnd) Sleep(100);
	}
	if (!hwnd) { InitFail(L"没有找到魔兽争霸窗口（窗口类名 Warcraft III）"); return 1; }
	GameHwnd = hwnd;
	Shared->gameHwnd = static_cast<DWORD>(reinterpret_cast<uintptr_t>(hwnd));
	CommandMessage = RegisterWindowMessageW(W3T_MSG_NAME);
	MessageFilterAllow(hwnd, CommandMessage);
	OriginalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(BridgeWndProc)));
	if (!OriginalWndProc) { InitFail(L"子类化魔兽窗口失败"); return 1; }

	// 5. 让主线程启动定时器
	InterlockedExchange(&Shared->dllState, DLL_READY);
	PostMessageW(hwnd, CommandMessage, W3T_WP_PING, 0);
	return 0;
}

void Bridge_Attach(HMODULE self) {
	SelfModule = self;
	HANDLE thread = CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
	if (thread) CloseHandle(thread);
}
