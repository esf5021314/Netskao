// 模块说明：
// 注入模块与界面程序之间的桥：
//   1. 初始化线程：等待 Game.dll → 识别版本 → 初始化偏移表 → 创建共享内存 →
//      在魔兽主线程上安装 WH_GETMESSAGE 线程钩子（失败时退回窗口子类化）；
//   2. 钩子（运行在游戏主线程）：主线程取出界面投递的命令消息时执行命令；
//      第一次收到消息时 SetTimer，之后每 100ms 刷新状态；
//   3. 看门狗线程：魔兽重建窗口时重新登记；子类化模式下窗口过程被替换时重新挂上；
//   4. 卸载：还原补丁、卸下钩子，然后 FreeLibraryAndExitThread。
//
// 为什么用窗口消息而不是原版的“Hook 每帧函数”：
//   魔兽的游戏逻辑与消息循环在同一个主线程里，消息钩子里调用 JASS 原生函数与在
//   每帧函数里调用是同一个线程、同一个时机；而且不需要任何版本相关的地址，
//   换版本不用重新找 Hook 点，也不需要改写游戏代码。
// 为什么首选线程钩子而不是窗口子类化：
//   Game.dll 的图形设备代码（1.24E 0x6BD63B / 0x6BEF89 / 0x6C113C 等）会自己
//   SetWindowLongA(GWL_WNDPROC) 替换 / 还原主窗口的窗口过程，设备重建（全屏切换、
//   改分辨率）后子类化会被挤掉；线程钩子挂在线程上，不受窗口过程替换影响。
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
static HWND volatile GameHwnd = NULL;       // 当前魔兽主窗口（看门狗会更新）
static DWORD MainThreadId = 0;              // 魔兽主线程（窗口所属线程）
static HHOOK MessageHook = NULL;            // WH_GETMESSAGE 线程钩子
static bool UseSubclass = false;            // 钩子安装失败时退回窗口子类化
static WNDPROC volatile OriginalWndProc = NULL;
static UINT CommandMessage = 0;
static HANDLE SharedHandle = NULL;
static W3T_Shared* Shared = NULL;
static LONG ProcessedSeq = 0;
static HWND TimerHwnd = NULL;               // 定时器所在的窗口（窗口重建后需要重新设置）
static volatile LONG Detached = 0;
static HANDLE WatchdogThread = NULL;
static CRITICAL_SECTION SubclassLock;       // 子类化模式下：看门狗与卸载互斥

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
// 子类化（钩子安装失败时的后备方案）
// ---------------------------------------------------------------------------

// 把窗口过程换成 BridgeWndProc；先读出原过程再替换，避免替换瞬间主线程用到空的 OriginalWndProc
static bool SubclassInstall(HWND hwnd) {
	EnterCriticalSection(&SubclassLock);
	bool ok = true;
	WNDPROC current = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(hwnd, GWLP_WNDPROC));
	if (current != BridgeWndProc) {
		if (current) OriginalWndProc = current;
		WNDPROC previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(BridgeWndProc)));
		if (previous && previous != BridgeWndProc) OriginalWndProc = previous;
		ok = previous != NULL;
	}
	LeaveCriticalSection(&SubclassLock);
	return ok;
}

// ---------------------------------------------------------------------------
// 卸载
// ---------------------------------------------------------------------------
static DWORD WINAPI UnloadThread(LPVOID) {
	if (WatchdogThread) {
		WaitForSingleObject(WatchdogThread, 5000);
		CloseHandle(WatchdogThread);
		WatchdogThread = NULL;
	}
	Sleep(500);     // 等主线程从钩子 / 窗口过程返回
	FreeLibraryAndExitThread(SelfModule, 0);
	return 0;
}

static void BridgeUnload() {
	if (Detached) return;
	if (TimerHwnd) KillTimer(TimerHwnd, W3T_TIMER_ID);
	TimerHwnd = NULL;
	Trainer_Shutdown(Shared);

	bool canFree = true;
	if (MessageHook) {
		UnhookWindowsHookEx(MessageHook);
		MessageHook = NULL;
	}
	if (UseSubclass) {
		// 只有窗口过程仍然是我们的时候才能还原；否则说明其它插件在我们之后又子类化了窗口，
		// 这时卸载会破坏它的调用链，只能留在内存里做透传。
		EnterCriticalSection(&SubclassLock);
		canFree = false;
		HWND hwnd = GameHwnd;
		if (OriginalWndProc && hwnd &&
			reinterpret_cast<WNDPROC>(GetWindowLongPtrA(hwnd, GWLP_WNDPROC)) == BridgeWndProc) {
			SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OriginalWndProc));
			canFree = true;
		}
		LeaveCriticalSection(&SubclassLock);
	}
	InterlockedExchange(&Detached, 1);     // 看门狗看到后退出
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
// 消息处理（游戏主线程）
// ---------------------------------------------------------------------------
static void CommandsDrain() {
	// 依次处理 ProcessedSeq+1 .. cmdSeq；环形缓冲 32 格，超出的旧命令丢弃
	LONG last = Shared->cmdSeq;
	if (last - ProcessedSeq > W3T_CMD_RING) ProcessedSeq = last - W3T_CMD_RING;
	while (ProcessedSeq < last) {
		LONG seq = ++ProcessedSeq;
		// 顺序锁读取：界面写槽位时先把 seq 清 0、写参数、最后写 seq；
		// 复制前后两次读到的 seq 都等于本序号，才说明复制的是完整的一条命令
		W3T_CmdSlot* source = &Shared->ring[seq % W3T_CMD_RING];
		if (InterlockedCompareExchange(&source->seq, 0, 0) != seq) continue;
		const volatile W3T_CmdSlot* v = source;
		W3T_CmdSlot slot;
		slot.cmd = v->cmd;
		slot.iarg = v->iarg;
		slot.farg = v->farg;
		if (InterlockedCompareExchange(&source->seq, 0, 0) != seq) continue;
		slot.seq = seq;
		Trainer_Execute(Shared, slot);
	}
}

// 处理我们自己的消息，已处理返回 true
static bool BridgeMessage(HWND hwnd, UINT message, WPARAM wParam) {
	if (Detached || !Shared) return false;
	if (message == CommandMessage && CommandMessage) {
		// 定时器挂在收到命令的窗口上；魔兽重建窗口后看门狗会投递 PING，这里重新设置
		if (hwnd && hwnd != TimerHwnd && IsWindow(hwnd)) {
			if (TimerHwnd && IsWindow(TimerHwnd)) KillTimer(TimerHwnd, W3T_TIMER_ID);
			TimerHwnd = SetTimer(hwnd, W3T_TIMER_ID, 100, NULL) ? hwnd : NULL;
		}
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
		return true;
	}
	if (message == WM_TIMER && wParam == W3T_TIMER_ID && hwnd && hwnd == TimerHwnd) {
		InterlockedIncrement(&Shared->heartbeat);
		Trainer_Tick(Shared);
		return true;
	}
	return false;
}

// WH_GETMESSAGE 线程钩子：主线程从队列取出消息（GetMessage / PeekMessage 带 PM_REMOVE）时调用
static LRESULT CALLBACK BridgeGetMessageProc(int code, WPARAM wParam, LPARAM lParam) {
	if (code == HC_ACTION && wParam == PM_REMOVE && lParam) {
		MSG* msg = reinterpret_cast<MSG*>(lParam);
		if (BridgeMessage(msg->hwnd, msg->message, msg->wParam)) {
			// 已处理：改成 WM_NULL，游戏的窗口过程不会再看到它
			msg->message = WM_NULL;
			msg->wParam = 0;
			msg->lParam = 0;
		}
	}
	return CallNextHookEx(NULL, code, wParam, lParam);
}

LRESULT CALLBACK BridgeWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (BridgeMessage(hwnd, message, wParam)) return 0;
	WNDPROC original = OriginalWndProc;
	return original ? CallWindowProcA(original, hwnd, message, wParam, lParam)
		: DefWindowProcA(hwnd, message, wParam, lParam);
}

// ---------------------------------------------------------------------------
// 看门狗线程
// ---------------------------------------------------------------------------
static DWORD WINAPI WatchdogProc(LPVOID) {
	while (!Detached) {
		Sleep(1000);
		if (Detached) break;
		HWND hwnd = GameHwnd;
		if (!hwnd || !IsWindow(hwnd)) {
			// 魔兽重建了主窗口：找到新窗口后重新登记
			HWND found = GameWindowFind();
			if (!found || Detached) continue;
			GameHwnd = found;
			hwnd = found;
			MessageFilterAllow(found, CommandMessage);
			if (UseSubclass) SubclassInstall(found);
			if (Shared && !Detached) Shared->gameHwnd = static_cast<DWORD>(reinterpret_cast<uintptr_t>(found));
			PostMessageW(found, CommandMessage, W3T_WP_PING, 0);     // 让主线程在新窗口上重设定时器
		}
		if (UseSubclass && !Detached &&
			reinterpret_cast<WNDPROC>(GetWindowLongPtrA(hwnd, GWLP_WNDPROC)) != BridgeWndProc) {
			// 窗口过程被游戏或其它插件替换，重新挂上
			SubclassInstall(hwnd);
		}
		if (MessageHook && !Detached) {
			DWORD thread = GetWindowThreadProcessId(hwnd, NULL);
			if (thread && thread != MainThreadId) {
				// 窗口换到了别的线程（极少见）：钩子跟着换
				HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, BridgeGetMessageProc, SelfModule, thread);
				if (hook) {
					UnhookWindowsHookEx(MessageHook);
					MessageHook = hook;
					MainThreadId = thread;
					PostMessageW(hwnd, CommandMessage, W3T_WP_PING, 0);
				}
			}
		}
	}
	return 0;
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
	if (!Shared) {
		// 映射失败：关掉句柄，让这块共享内存消失，界面会按“模块无法连接”报错，而不是一直等待
		CloseHandle(SharedHandle);
		SharedHandle = NULL;
		return 1;
	}
	if (!existed || Shared->magic != W3T_SHARED_MAGIC) {
		ZeroMemory(Shared, sizeof(W3T_Shared));
		Shared->inGameMessages = 1;
	}
	InterlockedExchange(&Shared->dllState, DLL_STARTING);
	Shared->abiVersion = W3T_ABI_VERSION;
	Shared->structSize = sizeof(W3T_Shared);
	for (int i = 0; i < W3T_TOGGLE_SLOTS; ++i) {
		Shared->toggleState[i] = 0;
		Shared->toggleWant[i] = 0;
	}
	ProcessedSeq = Shared->cmdSeq;      // 忽略上一次会话遗留的命令
	// magic 最后写：界面看到 magic 时，版本号等头部字段一定已经写好
	InterlockedExchange(reinterpret_cast<volatile LONG*>(&Shared->magic), static_cast<LONG>(W3T_SHARED_MAGIC));

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

	// 4. 找到魔兽主窗口（窗口可能比 Game.dll 晚创建），在它的线程上安装消息钩子
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
	InitializeCriticalSection(&SubclassLock);
	MainThreadId = GetWindowThreadProcessId(hwnd, NULL);
	MessageHook = SetWindowsHookExW(WH_GETMESSAGE, BridgeGetMessageProc, SelfModule, MainThreadId);
	if (!MessageHook) {
		UseSubclass = true;
		if (!SubclassInstall(hwnd)) { InitFail(L"安装消息钩子和子类化魔兽窗口都失败了"); return 1; }
	}

	// 5. 看门狗；让主线程启动定时器
	WatchdogThread = CreateThread(NULL, 0, WatchdogProc, NULL, 0, NULL);
	InterlockedExchange(&Shared->dllState, DLL_READY);
	PostMessageW(hwnd, CommandMessage, W3T_WP_PING, 0);
	return 0;
}

void Bridge_Attach(HMODULE self) {
	SelfModule = self;
	HANDLE thread = CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
	if (thread) CloseHandle(thread);
}
