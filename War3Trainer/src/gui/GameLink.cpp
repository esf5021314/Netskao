// 模块说明：与游戏的连接，见 GameLink.h
#include "stdafx.h"
#include "GameLink.h"
#include <tlhelp32.h>

static LinkInfo Info;
static wchar_t ErrorText[256];
static HANDLE Process = NULL;
static HANDLE SharedHandle = NULL;
static W3T_Shared* Shared = NULL;
static UINT CommandMessage = 0;
static DWORD InjectTick = 0;            // 注入时间，用于判断模块无响应
static DWORD ErrorTick = 0;             // 出错时间，出错后 3 秒再重试
static bool InjectedOnce = false;       // 本次连接已注入过，避免重复注入
static bool InGameMessages = true;

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------
static bool SelfElevated() {
	HANDLE token = NULL;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
	TOKEN_ELEVATION elevation;
	DWORD size = 0;
	bool result = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
	CloseHandle(token);
	return result;
}

static void DebugPrivilegeEnable() {
	HANDLE token = NULL;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return;
	TOKEN_PRIVILEGES tp;
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (LookupPrivilegeValueW(NULL, L"SeDebugPrivilege", &tp.Privileges[0].Luid)) {
		AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL);
	}
	CloseHandle(token);
}

static void ErrorSet(const wchar_t* format, ...) {
	va_list args;
	va_start(args, format);
	_vsnwprintf(ErrorText, 255, format, args);
	va_end(args);
	ErrorText[255] = 0;
	Info.phase = LINK_ERROR;
	ErrorTick = GetTickCount();
}

static void SharedClose() {
	if (Shared) { UnmapViewOfFile(Shared); Shared = NULL; }
	if (SharedHandle) { CloseHandle(SharedHandle); SharedHandle = NULL; }
}

static void Disconnect() {
	SharedClose();
	if (Process) { CloseHandle(Process); Process = NULL; }
	Info.pid = 0;
	Info.hwnd = NULL;
	Info.exeName[0] = 0;
	InjectedOnce = false;
	InjectTick = 0;
}

// 进程中是否已加载指定模块
static bool ModuleLoaded(DWORD pid, const wchar_t* name, bool* snapshotFailed) {
	*snapshotFailed = false;
	HANDLE snap = INVALID_HANDLE_VALUE;
	for (int i = 0; i < 5; ++i) {           // ERROR_BAD_LENGTH 时需要重试
		snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
		if (snap != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
	}
	if (snap == INVALID_HANDLE_VALUE) { *snapshotFailed = true; return false; }
	MODULEENTRY32W me;
	me.dwSize = sizeof(me);
	bool found = false;
	if (Module32FirstW(snap, &me)) {
		do {
			if (lstrcmpiW(me.szModule, name) == 0) { found = true; break; }
		} while (Module32NextW(snap, &me));
	}
	CloseHandle(snap);
	return found;
}

static void ProcessNameGet(DWORD pid, wchar_t* out, int outSize) {
	out[0] = 0;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return;
	PROCESSENTRY32W pe;
	pe.dwSize = sizeof(pe);
	if (Process32FirstW(snap, &pe)) {
		do {
			if (pe.th32ProcessID == pid) { lstrcpynW(out, pe.szExeFile, outSize); break; }
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
}

static bool DllPathGet(wchar_t* path, int size) {
	GetModuleFileNameW(NULL, path, size);
	wchar_t* slash = wcsrchr(path, L'\\');
	if (!slash) return false;
	slash[1] = 0;
	wcsncat(path, W3T_DLL_NAME, size - wcslen(path) - 1);
	return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// CreateRemoteThread(LoadLibraryW, dll 路径)
static bool Inject() {
	wchar_t path[MAX_PATH];
	if (!DllPathGet(path, MAX_PATH)) {
		ErrorSet(L"找不到 %ls，请把它和 War3Trainer.exe 放在同一个目录", W3T_DLL_NAME);
		return false;
	}
	SIZE_T bytes = (wcslen(path) + 1) * sizeof(wchar_t);
	void* remote = VirtualAllocEx(Process, NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remote) { ErrorSet(L"在游戏进程中申请内存失败（错误码 %lu）", GetLastError()); return false; }
	if (!WriteProcessMemory(Process, remote, path, bytes, NULL)) {
		ErrorSet(L"写入游戏进程失败（错误码 %lu）", GetLastError());
		VirtualFreeEx(Process, remote, 0, MEM_RELEASE);
		return false;
	}
	// 32 位进程中 kernel32 的加载地址在同一次开机内相同
	LPTHREAD_START_ROUTINE loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
		reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
	HANDLE thread = CreateRemoteThread(Process, NULL, 0, loadLibrary, remote, 0, NULL);
	if (!thread) {
		ErrorSet(L"创建远程线程失败（错误码 %lu），可能被杀毒软件拦截", GetLastError());
		VirtualFreeEx(Process, remote, 0, MEM_RELEASE);
		return false;
	}
	DWORD wait = WaitForSingleObject(thread, 10000);
	DWORD exitCode = 0;
	GetExitCodeThread(thread, &exitCode);
	CloseHandle(thread);
	if (wait == WAIT_OBJECT_0) VirtualFreeEx(Process, remote, 0, MEM_RELEASE);
	if (wait != WAIT_OBJECT_0) { ErrorSet(L"注入超时"); return false; }
	if (exitCode == 0) {
		ErrorSet(L"游戏加载 %ls 失败（文件损坏或被杀毒软件拦截）", W3T_DLL_NAME);
		return false;
	}
	InjectedOnce = true;
	InjectTick = GetTickCount();
	return true;
}

static bool SharedOpen() {
	if (Shared) return true;
	wchar_t name[64];
	_snwprintf(name, 63, W3T_SHM_NAME_FMT, Info.pid);
	name[63] = 0;
	SharedHandle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
	if (!SharedHandle) return false;
	Shared = static_cast<W3T_Shared*>(MapViewOfFile(SharedHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(W3T_Shared)));
	if (!Shared) { SharedClose(); return false; }
	return true;
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
void GameLink_Init() {
	ZeroMemory(&Info, sizeof(Info));
	Info.elevated = SelfElevated();
	DebugPrivilegeEnable();
	CommandMessage = RegisterWindowMessageW(W3T_MSG_NAME);
}

void GameLink_Poll() {
	DWORD now = GetTickCount();

	// 游戏退出：断开，重新开始查找
	if (Process && WaitForSingleObject(Process, 0) == WAIT_OBJECT_0) {
		Disconnect();
		Info.phase = LINK_SEARCHING;
	}
	if (Info.phase == LINK_ERROR) {
		if (now - ErrorTick < 3000) return;
		Disconnect();
		Info.phase = LINK_SEARCHING;
	}

	if (!Process) {
		HWND hwnd = FindWindowW(L"Warcraft III", NULL);
		if (!hwnd) { Info.phase = LINK_SEARCHING; return; }
		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if (!pid) return;
		Info.hwnd = hwnd;
		Info.pid = pid;
		ProcessNameGet(pid, Info.exeName, 64);
		Process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
			PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
		if (!Process) {
			DWORD err = GetLastError();
			if (err == ERROR_ACCESS_DENIED) {
				ErrorSet(Info.elevated ? L"打开游戏进程被拒绝（错误码 5），可能被杀毒软件或对战平台保护"
					: L"权限不足：游戏以管理员身份运行，请右键修改器选择“以管理员身份运行”");
			} else {
				ErrorSet(L"打开游戏进程失败（错误码 %lu）", err);
			}
			return;
		}
		Info.phase = LINK_OPENING;
	}

	// 已连接：检查模块状态
	if (Shared) {
		if (Shared->magic != W3T_SHARED_MAGIC) return;
		if (Shared->abiVersion != W3T_ABI_VERSION) {
			ErrorSet(L"游戏里已加载另一个版本的修改器模块，请重启游戏后再连接");
			return;
		}
		switch (Shared->dllState) {
		case DLL_READY:
			Info.phase = LINK_READY;
			return;
		case DLL_FAILED:
			ErrorSet(L"%ls", Shared->initError);
			return;
		case DLL_UNLOADED:
			// 上一次退出时模块已卸载，重新注入
			SharedClose();
			InjectedOnce = false;
			break;
		default:
			Info.phase = LINK_INJECTING;
			if (InjectTick && now - InjectTick > 20000) ErrorSet(L"修改器模块初始化超时（没有找到魔兽窗口？）");
			return;
		}
	}

	// 等待 Game.dll 加载（从启动器启动时会晚一点）
	bool snapshotFailed = false;
	if (!ModuleLoaded(Info.pid, L"Game.dll", &snapshotFailed)) {
		if (snapshotFailed) ErrorSet(L"读取游戏模块列表失败（错误码 %lu）", GetLastError());
		else Info.phase = LINK_OPENING;
		return;
	}

	// 模块已在游戏里（上次运行留下的）就直接连接，否则注入
	if (SharedOpen()) { Info.phase = LINK_INJECTING; return; }
	if (ModuleLoaded(Info.pid, W3T_DLL_NAME, &snapshotFailed)) {
		Info.phase = LINK_INJECTING;
		if (!InjectTick) InjectTick = now;
		if (now - InjectTick > 20000) ErrorSet(L"游戏里已有修改器模块但无法连接，请重启游戏");
		return;
	}
	if (!InjectedOnce) {
		if (!Inject()) return;
		Info.phase = LINK_INJECTING;
	} else if (now - InjectTick > 20000) {
		ErrorSet(L"注入后修改器模块没有响应");
	}
}

void GameLink_Shutdown(bool unloadModule) {
	if (Shared && unloadModule && Shared->dllState == DLL_READY && Info.hwnd) {
		PostMessageW(Info.hwnd, CommandMessage, W3T_WP_UNLOAD, 0);
		// 等模块还原补丁（最多 1.5 秒；游戏卡在加载界面时可能来不及处理）
		for (int i = 0; i < 30 && Shared->dllState != DLL_UNLOADED; ++i) Sleep(50);
	}
	Disconnect();
}

const LinkInfo& GameLink_Info() { return Info; }
const wchar_t* GameLink_Error() { return ErrorText; }
W3T_Shared* GameLink_Shared() { return (Shared && Info.phase == LINK_READY) ? Shared : NULL; }

LONG GameLink_SendCommand(int cmd, int iarg, float farg) {
	W3T_Shared* shm = GameLink_Shared();
	if (!shm) return 0;
	LONG seq = shm->cmdSeq + 1;
	W3T_CmdSlot& slot = shm->ring[seq % W3T_CMD_RING];
	InterlockedExchange(&slot.seq, 0);      // 先作废槽位，再写参数，最后写序号
	slot.cmd = cmd;
	slot.iarg = iarg;
	slot.farg = farg;
	InterlockedExchange(&slot.seq, seq);
	InterlockedExchange(&shm->cmdSeq, seq);
	shm->inGameMessages = InGameMessages ? 1 : 0;
	if (!PostMessageW(Info.hwnd, CommandMessage, W3T_WP_COMMAND, seq)) return 0;
	return seq;
}

bool GameLink_SetToggle(int toggleId, bool want, int argument) {
	W3T_Shared* shm = GameLink_Shared();
	if (!shm || toggleId < 0 || toggleId >= W3T_TOGGLE_SLOTS) return false;
	shm->toggleArg[toggleId] = argument;
	shm->toggleWant[toggleId] = want ? 1 : 0;
	shm->inGameMessages = InGameMessages ? 1 : 0;
	return PostMessageW(Info.hwnd, CommandMessage, W3T_WP_SYNC_TOGGLES, 0) != FALSE;
}

void GameLink_SetInGameMessages(bool enable) {
	InGameMessages = enable;
	if (Shared) Shared->inGameMessages = enable ? 1 : 0;
}
