// 模块说明：
// 注入模块与界面程序之间的桥：
//   1. 初始化线程：创建共享内存 → 等待 Game.dll → 识别版本 → 初始化偏移表 →
//      安装每帧挂钩 → 启动后台线程；
//   2. 每帧挂钩（游戏线程，只在对局画面刷新时运行）：执行界面写入的命令、同步常驻开关，
//      每 100ms 刷新一次状态（选中单位 / 无CD无蓝耗 / 清理施法单位）；
//   3. 后台线程（每 100ms）：心跳；对局画面没有刷新时（主菜单 / 读图 / 窗口最小化）
//      代为回复命令、在退出对局时还原代码补丁；处理卸载请求；
//   4. 卸载：还原补丁、卸下挂钩，由后台线程最后 FreeLibraryAndExitThread。
//
// 为什么用每帧挂钩（与原版 CE 脚本相同），而不是窗口消息：
//   早期版本通过 PostMessage + 消息钩子让游戏主线程执行命令，在模拟环境里正常，
//   但在真实的 1.24E 中这些消息从未被处理（界面显示“游戏暂时无响应”，快捷键没有任何反应）。
//   每帧挂钩不依赖窗口和消息循环，是原版修改器实机验证过的做法；挂钩点在 18 个版本中
//   都已用特征码定位（见 Offsets.cpp）。
//
// 线程约定：Trainer 的状态只在持有 Lock 时访问。每帧挂钩用 TryEnterCriticalSection，
// 永远不会让游戏线程等待；后台线程只读内存、还原代码字节，不调用任何游戏函数。
#include "stdafx.h"
#include "Bridge.h"
#include "Offsets.h"
#include "Jass.h"
#include "Patch.h"
#include "Trainer.h"
#include "SafeCall.h"
#include "../common/Shared.h"

static HMODULE SelfModule = NULL;
static HANDLE SharedHandle = NULL;
static W3T_Shared* Shared = NULL;
static CRITICAL_SECTION Lock;               // 每帧挂钩与后台线程互斥
static LONG ProcessedSeq = 0;               // 已处理到的命令序号
static LONG SyncedToggleSeq = 0;            // 已同步到的开关序号
static volatile LONG Detached = 0;          // 1 = 已卸载（挂钩里立即返回）
static volatile LONG InFrame = 0;           // 防重入
static volatile DWORD LastFrameTick = 0;    // 最近一帧的时间（0 = 还没有帧）
static DWORD LastTickTime = 0;              // 最近一次 Trainer_Tick 的时间
static bool CanFree = true;                 // 挂钩已卸下，可以释放模块

static const DWORD kFrameStallMs = 500;     // 超过这么久没有帧：对局画面没有在刷新
static const DWORD kTickMs = 100;           // 状态刷新间隔

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
// 卸载（持有 Lock 时调用）
// ---------------------------------------------------------------------------
static void BridgeUnload(bool gameThread) {
	if (Detached) return;
	Trainer_Shutdown(Shared, gameThread);
	// 挂钩点被其它程序改过时卸不下来：存根还会调用模块里的代码，模块只能留在内存里（挂钩里立即返回）
	CanFree = FrameHookRemove();
	InterlockedExchange(&Detached, 1);
	if (Shared) {
		InterlockedExchange(&Shared->dllState, DLL_UNLOADED);
		UnmapViewOfFile(Shared);
		Shared = NULL;
	}
	if (SharedHandle) {
		CloseHandle(SharedHandle);
		SharedHandle = NULL;
	}
}

// ---------------------------------------------------------------------------
// 命令与开关（持有 Lock 时调用）
// ---------------------------------------------------------------------------
// 依次处理 ProcessedSeq+1 .. cmdSeq；环形缓冲 32 格，超出的旧命令丢弃。
// gameThread = true：在游戏线程执行；false：对局画面没有刷新，由后台线程代为回复
static void CommandsDrain(bool gameThread, bool inGame) {
	LONG last = Shared->cmdSeq;
	if (last - ProcessedSeq > W3T_CMD_RING) ProcessedSeq = last - W3T_CMD_RING;
	while (last - ProcessedSeq > 0) {
		LONG seq = ++ProcessedSeq;
		// 顺序锁读取：界面写槽位时先把 seq 清 0、写参数、最后写 seq；
		// 复制前后两次读到的 seq 都等于本序号，才说明复制的是完整的一条命令
		W3T_CmdSlot* source = &Shared->ring[(DWORD)seq % W3T_CMD_RING];
		if (InterlockedCompareExchange(&source->seq, 0, 0) != seq) continue;
		const volatile W3T_CmdSlot* v = source;
		W3T_CmdSlot slot;
		slot.cmd = v->cmd;
		slot.iarg = v->iarg;
		slot.farg = v->farg;
		if (InterlockedCompareExchange(&source->seq, 0, 0) != seq) continue;
		slot.seq = seq;
		if (gameThread) Trainer_Execute(Shared, slot);
		else Trainer_Reject(Shared, slot, inGame);
	}
}

// 界面改了开关（toggleSeq 变化），或者还有没处理完的开关
static bool ToggleSyncNeeded() {
	if (Shared->toggleSeq != SyncedToggleSeq) return true;
	for (int id = 0; id < TGL_COUNT; ++id) {
		if ((Shared->toggleWant[id] != 0) != (Shared->toggleState[id] != 0)) return true;
	}
	return false;
}

static void TogglesSync(bool gameThread) {
	if (!ToggleSyncNeeded()) return;
	SyncedToggleSeq = Shared->toggleSeq;
	Trainer_SyncToggles(Shared, gameThread);
}

// ---------------------------------------------------------------------------
// 每帧挂钩（游戏线程）：由 Patch.cpp 的存根在 CWorldFrameWar3 每帧函数里调用
// ---------------------------------------------------------------------------
#if defined(__GNUC__)
__attribute__((force_align_arg_pointer))    // 游戏代码只保证 4 字节栈对齐
#endif
static void FrameEntry() {
	if (Detached) return;
	if (InterlockedExchange(&InFrame, 1)) return;
	if (TryEnterCriticalSection(&Lock)) {        // 后台线程正在处理时跳过这一帧，不让游戏等待
		if (!Detached && Shared) {
			SafeRun([&] {
				DWORD now = GetTickCount();
				LastFrameTick = now ? now : 1;
				InterlockedIncrement(&Shared->frameCount);
				Shared->frameActive = 1;
				Shared->frameThreadId = static_cast<LONG>(GetCurrentThreadId());
				if (Shared->unloadRequest) {
					BridgeUnload(true);
					return;
				}
				CommandsDrain(true, true);
				TogglesSync(true);
				if (now - LastTickTime >= kTickMs) {
					LastTickTime = now;
					Trainer_Tick(Shared);
				}
			});
		}
		LeaveCriticalSection(&Lock);
	}
	InterlockedExchange(&InFrame, 0);
}

// ---------------------------------------------------------------------------
// 后台线程
// ---------------------------------------------------------------------------
static DWORD WINAPI ServiceThread(LPVOID) {
	while (!Detached) {
		Sleep(kTickMs);
		EnterCriticalSection(&Lock);
		if (!Detached && Shared) {
			InterlockedIncrement(&Shared->heartbeat);
			DWORD last = LastFrameTick;
			bool frames = last && GetTickCount() - last < kFrameStallMs;
			if (!frames) {
				// 对局画面没有在刷新：主菜单 / 读图 / 窗口最小化。不能调用游戏函数，只做不需要游戏线程的事
				Shared->frameActive = 0;
				SafeRun([&] {
					if (Shared->unloadRequest) {
						BridgeUnload(false);
						return;
					}
					bool inGame = Trainer_Idle(Shared);
					CommandsDrain(false, inGame);
					TogglesSync(false);
				});
			}
		}
		LeaveCriticalSection(&Lock);
	}
	if (!CanFree) return 0;
	Sleep(500);     // 等游戏线程从挂钩里返回
	FreeLibraryAndExitThread(SelfModule, 0);
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
	Shared->unloadRequest = 0;
	for (int i = 0; i < W3T_TOGGLE_SLOTS; ++i) {
		Shared->toggleState[i] = 0;
		Shared->toggleWant[i] = 0;
	}
	ProcessedSeq = Shared->cmdSeq;          // 忽略上一次会话遗留的命令
	SyncedToggleSeq = Shared->toggleSeq;
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
	DWORD toggles = 1u << TGL_NOCD_NOMANA;          // 由每帧刷新维持，只依赖原生函数
	for (int id = 0; id < TGL_COUNT; ++id) {
		if (id != TGL_NOCD_NOMANA && PatchSupported(id)) toggles |= 1u << id;
	}
	Shared->toggleSupport = toggles;
	if (!(support & W3T_SUPPORT_NATIVES)) {
		Shared->supportFlags = support;
		wchar_t text[128];
		_snwprintf(text, 127, L"不支持的游戏版本 %ls（build %lu），函数表缺少编号 %d", Shared->versionName, build, missing);
		text[127] = 0;
		InitFail(text);
		return 1;
	}

	// 4. 每帧挂钩（锁要先准备好：挂上之后的下一帧就会调用 FrameEntry）
	InitializeCriticalSection(&Lock);
	wchar_t reason[160];
	reason[0] = 0;
	if (!FrameHookInstall(FrameEntry, reason, 160)) {
		Shared->supportFlags = support;
		InitFail(reason[0] ? reason : L"安装每帧挂钩失败");
		return 1;
	}
	support |= W3T_SUPPORT_FRAMEHOOK;
	Shared->supportFlags = support;
	Shared->hookAddress = reinterpret_cast<DWORD>(Offset(GAME_FRAME_HOOK));

	// 5. 后台线程
	HANDLE service = CreateThread(NULL, 0, ServiceThread, NULL, 0, NULL);
	if (!service) {
		EnterCriticalSection(&Lock);
		if (FrameHookRemove()) InterlockedExchange(&Detached, 1);
		LeaveCriticalSection(&Lock);
		InitFail(L"创建修改器后台线程失败");
		return 1;
	}
	CloseHandle(service);
	InterlockedExchange(&Shared->dllState, DLL_READY);
	return 0;
}

void Bridge_Attach(HMODULE self) {
	SelfModule = self;
	HANDLE thread = CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
	if (thread) CloseHandle(thread);
}
