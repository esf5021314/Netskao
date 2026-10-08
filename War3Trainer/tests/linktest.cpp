// 模块说明：
// 连接链路测试（控制台）。配合 tests/mock 的模拟魔兽进程使用（映射真实的 1.24E Game.dll）：
//   build/mock/war3.exe <1.24E Game.dll 路径>     然后运行 build/mock/linktest.exe
// 检查：查找窗口 → 注入 War3Trainer.dll → 共享内存 → 版本与协议 → 每帧挂钩已写入真实 Game.dll
//   → 没有帧时后台线程回复命令 / 拒绝开启开关 → 模拟每帧后命令在游戏线程执行
//   → 结果环形区 → 对局中 / 不在对局中两种情况下卸载都能还原挂钩 → 重新注入
#include "../src/gui/stdafx.h"
#include "../src/gui/GameLink.h"
#include <stdio.h>
#include <string.h>

static int Failures = 0;

// 控制台按 UTF-8 输出中文
static const char* U8(const wchar_t* text) {
	static char buffers[4][512];
	static int index = 0;
	char* out = buffers[index++ & 3];
	WideCharToMultiByte(CP_UTF8, 0, text, -1, out, 512, NULL, NULL);
	return out;
}
#define CHECK(cond, ...) do { if (!(cond)) { ++Failures; printf("  [失败] " __VA_ARGS__); printf("\n"); } else { printf("  [通过] " __VA_ARGS__); printf("\n"); } } while (0)

static const DWORD kHookRva = 0x4D3E30;
static const char* kHookOrig = "890DF0ECAC6F";     // mov [0x6FACECF0], ecx

static bool WaitReady(int ms) {
	for (int t = 0; t < ms; t += 250) {
		GameLink_Poll();
		if (GameLink_Info().phase == LINK_READY) return true;
		if (GameLink_Info().phase == LINK_ERROR) { printf("  错误：%s\n", U8(GameLink_Error())); return false; }
		Sleep(250);
	}
	return false;
}

static bool WaitCounter(W3T_Shared* shm, LONG before, int ms) {
	for (int t = 0; t < ms; t += 20) {
		if (shm->resultCounter != before) return true;
		Sleep(20);
	}
	return false;
}

static bool WaitFlag(volatile LONG* flag, LONG value, int ms) {
	for (int t = 0; t < ms; t += 20) {
		if (*flag == value) return true;
		Sleep(20);
	}
	return *flag == value;
}

// 读游戏进程里挂钩点的 6 个字节
static void HookBytes(DWORD pid, DWORD base, char* out) {
	BYTE buf[6] = {};
	HANDLE process = OpenProcess(PROCESS_VM_READ, FALSE, pid);
	if (process) {
		ReadProcessMemory(process, reinterpret_cast<void*>(base + kHookRva), buf, 6, NULL);
		CloseHandle(process);
	}
	for (int i = 0; i < 6; ++i) sprintf(out + i * 2, "%02X", buf[i]);
}

static HWND MockWindow = NULL;      // 模拟进程的窗口（断开连接后 GameLink_Info().hwnd 会清空）
static void Frames(bool on) {
	SendMessageW(MockWindow, WM_APP + 78, on ? 1 : 0, 0);
}

int main() {
	SetConsoleOutputCP(CP_UTF8);
	GameLink_Init();
	GameLink_SetInGameMessages(false);      // 连接之前设置，连接后应推送给模块（模块初始化时默认为开启）
	printf("等待连接模拟游戏 ...\n");
	bool ready = WaitReady(30000);
	CHECK(ready, "连接成功（phase=%d）", GameLink_Info().phase);
	if (!ready) return 1;
	W3T_Shared* shm = GameLink_Shared();
	DWORD pid = GameLink_Info().pid;
	MockWindow = GameLink_Info().hwnd;
	DWORD base = shm->gameBase;
	CHECK(shm->gameBuild == 6387 && base == 0x6F000000, "识别版本 build=%lu 名称=%s 基址=%08lX", shm->gameBuild, U8(shm->versionName), base);
	CHECK(shm->structSize == sizeof(W3T_Shared) && shm->abiVersion == W3T_ABI_VERSION, "协议版本 %lu，结构大小 %lu", shm->abiVersion, shm->structSize);
	CHECK((shm->supportFlags & W3T_SUPPORT_NATIVES) && (shm->supportFlags & W3T_SUPPORT_VERIFIED) && (shm->supportFlags & W3T_SUPPORT_FRAMEHOOK),
		"支持位 %08lX（含每帧挂钩）", shm->supportFlags);
	CHECK(shm->hookAddress == base + kHookRva, "挂钩点 %08lX", shm->hookAddress);
	char hex[16];
	HookBytes(pid, base, hex);
	CHECK(hex[0] == 'E' && hex[1] == '9' && strcmp(hex + 10, "90") == 0, "真实 Game.dll 的挂钩点已改为 JMP：%s", hex);
	Sleep(300);
	CHECK(shm->inGameMessages == 0, "连接前关闭的“游戏内显示提示”已推送给模块");

	// ---- 没有帧（相当于主菜单）：后台线程回复 ----
	printf("没有帧（主菜单）：\n");
	LONG hb = shm->heartbeat;
	Sleep(600);
	CHECK(shm->heartbeat - hb >= 3, "后台线程心跳（%ld -> %ld）", hb, shm->heartbeat);
	CHECK(shm->frameCount == 0 && shm->frameActive == 0 && shm->inGame == 0, "没有帧，判定为不在对局中");

	LONG before = shm->resultCounter;
	LONG seq = GameLink_SendCommand(CMD_TELEPORT, 0, 0.0f);
	CHECK(seq > 0, "发送命令 seq=%ld", seq);
	CHECK(WaitCounter(shm, before, 3000), "收到命令结果");
	CHECK(shm->resultSeq == seq && shm->resultCmd == CMD_TELEPORT, "结果对应命令 seq=%ld cmd=%ld", shm->resultSeq, shm->resultCmd);
	CHECK(shm->resultCode == RES_NOT_IN_GAME, "结果码=%ld 文字=%s", shm->resultCode, U8(shm->resultText));

	before = shm->resultCounter;
	for (int i = 0; i < 10; ++i) GameLink_SendCommand(CMD_KILL, 0, 0.0f);
	Sleep(1000);
	CHECK(shm->resultCounter - before == 10, "连发 10 条命令全部回复（%ld 条）", shm->resultCounter - before);
	int ringOk = 0;
	for (LONG n = shm->resultCounter - 9; n <= shm->resultCounter; ++n) {
		const W3T_ResultEntry& e = shm->results[n % W3T_RESULT_RING];
		if (e.counter == n && e.cmd == CMD_KILL && e.code == RES_NOT_IN_GAME && e.text[0]) ++ringOk;
	}
	CHECK(ringOk == 10, "结果环形区保存了最近 10 条结果（%d 条完整）", ringOk);

	const int toggles[] = { TGL_NO_DEFEAT, TGL_NOCD_NOMANA };
	for (int k = 0; k < 2; ++k) {
		int id = toggles[k];
		before = shm->resultCounter;
		GameLink_SetToggle(id, true, 0);
		CHECK(WaitCounter(shm, before, 3000), "收到开关 %d 的结果", id);
		CHECK(shm->toggleState[id] == 0 && shm->toggleWant[id] == 0 && shm->resultCode == RES_NOT_IN_GAME,
			"不在对局中开启被拒绝，开关保持关闭：%s", U8(shm->resultText));
	}

	// ---- 模拟每帧：命令在游戏线程执行 ----
	printf("模拟每帧（对局画面）：\n");
	Frames(true);
	bool active = WaitFlag(&shm->frameActive, 1, 2000);
	LONG frames = shm->frameCount;
	Sleep(300);
	CHECK(active && shm->frameCount - frames >= 5, "每帧挂钩在运行（300ms 内 %ld 帧）", shm->frameCount - frames);
	DWORD mainThread = GetWindowThreadProcessId(MockWindow, NULL);
	CHECK((DWORD)shm->frameThreadId == mainThread, "挂钩运行在游戏主线程（%ld / %lu）", shm->frameThreadId, mainThread);
	before = shm->resultCounter;
	DWORD t0 = GetTickCount();
	seq = GameLink_SendCommand(CMD_HERO_LEVELUP, 3, 0.0f);
	bool got = WaitCounter(shm, before, 3000);
	DWORD latency = GetTickCount() - t0;
	CHECK(got && shm->resultSeq == seq && shm->frameActive, "对局画面中命令由游戏线程执行（%lu ms）：%s", latency, U8(shm->resultText));
	CHECK(shm->resultCode == RES_NOT_IN_GAME, "模拟进程没有真实对局，游戏线程判定为不在对局中（结果码 %ld）", shm->resultCode);
	before = shm->resultCounter;
	GameLink_SetToggle(TGL_NO_DEFEAT, true, 0);
	CHECK(WaitCounter(shm, before, 3000) && shm->toggleState[TGL_NO_DEFEAT] == 0 && shm->resultCode == RES_NOT_IN_GAME,
		"游戏线程同样拒绝在对局外开启开关：%s", U8(shm->resultText));
	Frames(false);
	CHECK(WaitFlag(&shm->frameActive, 0, 2000), "停止帧后判定为画面不再刷新");

	// ---- 卸载：对局画面中（游戏线程处理）----
	printf("卸载：\n");
	Frames(true);
	WaitFlag(&shm->frameActive, 1, 2000);
	GameLink_Shutdown(true);
	Sleep(300);
	HookBytes(pid, base, hex);
	CHECK(strcmp(hex, kHookOrig) == 0, "对局画面中卸载：挂钩点已还原 %s", hex);
	Frames(false);
	Sleep(1000);
	wchar_t name[64];
	_snwprintf(name, 63, W3T_SHM_NAME_FMT, pid);
	HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
	CHECK(h == NULL, "卸载后共享内存已释放");
	if (h) CloseHandle(h);

	// ---- 重新注入；没有帧时卸载（后台线程处理）----
	GameLink_Init();
	ready = WaitReady(30000);
	CHECK(ready, "卸载后可以重新注入并连接");
	if (ready) {
		shm = GameLink_Shared();
		HookBytes(pid, base, hex);
		CHECK(hex[0] == 'E' && hex[1] == '9', "重新挂钩 %s", hex);
		GameLink_Shutdown(true);
		Sleep(300);
		HookBytes(pid, base, hex);
		CHECK(strcmp(hex, kHookOrig) == 0, "主菜单中卸载：挂钩点已还原 %s", hex);
	}

	printf(Failures ? "\n共 %d 项失败\n" : "\n全部通过\n", Failures);
	return Failures ? 1 : 0;
}
