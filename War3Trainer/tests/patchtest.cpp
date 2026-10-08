// 模块说明：
// 补丁引擎测试（控制台）。配合以“真实 1.24E Game.dll”启动的模拟进程使用：
//   build/mock/war3.exe <1.24E Game.dll 路径>   然后运行 build/mock/patch/patchtest.exe
// 同目录的 War3Trainer.dll 是测试版（W3T_TEST_BUILD）：模拟进程里没有对局，测试版允许在对局外开启开关。
// 测试开始时让模拟进程“每帧”调用挂钩点，开关和真实游戏一样在游戏线程里切换。
// 逐个打开 / 关闭常驻开关，用 ReadProcessMemory 读回补丁处字节，确认写入与还原都正确；
// “英雄最大等级”打开后通过测试消息调用被挂钩的 GetMiscInt，确认返回设定值。
#include "../src/gui/stdafx.h"
#include "../src/gui/GameLink.h"
#include <stdio.h>
#include <string.h>

static int Failures = 0;
static const char* U8(const wchar_t* text) {
	static char buffers[4][512];
	static int index = 0;
	char* out = buffers[index++ & 3];
	WideCharToMultiByte(CP_UTF8, 0, text, -1, out, 512, NULL, NULL);
	return out;
}
#define CHECK(cond, ...) do { if (!(cond)) { ++Failures; printf("  [失败] " __VA_ARGS__); printf("\n"); } else { printf("  [通过] " __VA_ARGS__); printf("\n"); } } while (0)

struct Site { int toggle; DWORD rva; const char* orig; const char* patched; };
static const Site kSites[] = {
	{ TGL_NO_DEFEAT,  0x3BD430, "B90D000000",     "33C040C390" },
	{ TGL_FUN_MODE,   0x29C2DE, "8B401859C3",     "33C04059C3" },
	{ TGL_FUN_MODE,   0x29CDDE, "8B80B0010000",   "B80400000090" },
	{ TGL_FUN_MODE,   0x00AF71, "7E1DB82C010000", "9090B8FFFF0000" },
	{ TGL_FUN_MODE,   0x8882FC, "64000000",       "FFFF0000" },
	{ TGL_FUN_MODE,   0x2964E6, "E9D6000000",     "E9D4000000" },
	{ TGL_FUN_MODE,   0x296533, "E989000000",     "E987000000" },
	{ TGL_FUN_MODE,   0x296580, "EB3F",           "EB3D" },
	{ TGL_AURA_STACK, 0x0445DD, "751E",           "9090" },
};

static HANDLE Process;
static DWORD Base;

static void Hex(DWORD rva, int n, char* out) {
	BYTE buf[16] = {};
	ReadProcessMemory(Process, (void*)(Base + rva), buf, n, NULL);
	for (int i = 0; i < n; ++i) sprintf(out + i * 2, "%02X", buf[i]);
}

static bool Toggle(W3T_Shared* shm, int id, bool on, int arg) {
	LONG before = shm->resultCounter;
	GameLink_SetToggle(id, on, arg);
	for (int t = 0; t < 3000 && shm->resultCounter == before; t += 50) Sleep(50);
	printf("    %s\n", U8(shm->resultText));
	return shm->toggleState[id] == (on ? 1 : 0);
}

static void CheckSites(int toggle, bool on) {
	for (size_t i = 0; i < sizeof(kSites) / sizeof(kSites[0]); ++i) {
		if (kSites[i].toggle != toggle) continue;
		char hex[40];
		int n = (int)strlen(kSites[i].orig) / 2;
		Hex(kSites[i].rva, n, hex);
		const char* expect = on ? kSites[i].patched : kSites[i].orig;
		CHECK(strcmp(hex, expect) == 0, "Game.dll+%06lX = %s（期望 %s）", kSites[i].rva, hex, expect);
	}
}

int main() {
	SetConsoleOutputCP(CP_UTF8);
	GameLink_Init();
	bool ready = false;
	for (int t = 0; t < 30000 && !ready; t += 250) {
		GameLink_Poll();
		ready = GameLink_Info().phase == LINK_READY;
		if (GameLink_Info().phase == LINK_ERROR) { printf("错误：%s\n", U8(GameLink_Error())); return 1; }
		if (!ready) Sleep(250);
	}
	CHECK(ready, "连接到加载了真实 Game.dll 的模拟进程");
	if (!ready) return 1;
	W3T_Shared* shm = GameLink_Shared();
	Base = shm->gameBase;
	Process = OpenProcess(PROCESS_VM_READ, FALSE, GameLink_Info().pid);
	CHECK(shm->gameBuild == 6387 && Base == 0x6F000000, "真实 Game.dll：build=%lu 基址=%08lX", shm->gameBuild, Base);
	HWND hwnd = GameLink_Info().hwnd;
	SendMessageW(hwnd, WM_APP + 78, 1, 0);      // 开始模拟每帧
	for (int t = 0; t < 2000 && !shm->frameActive; t += 50) Sleep(50);
	CHECK(shm->frameActive && shm->frameThreadId == (LONG)GetWindowThreadProcessId(hwnd, NULL), "每帧挂钩运行在模拟进程主线程");
	char hook[40];
	Hex(0x4D3E30, 6, hook);
	CHECK(hook[0] == 'E' && hook[1] == '9', "每帧挂钩点 Game.dll+4D3E30 = %s", hook);

	const int toggles[] = { TGL_NO_DEFEAT, TGL_FUN_MODE, TGL_AURA_STACK };
	for (int k = 0; k < 3; ++k) {
		int id = toggles[k];
		printf("开关 %d：\n", id);
		CheckSites(id, false);
		CHECK(Toggle(shm, id, true, 0), "打开");
		CheckSites(id, true);
		CHECK(Toggle(shm, id, false, 0), "关闭");
		CheckSites(id, false);
	}

	printf("英雄最大等级：\n");
	char hex[40];
	Hex(0xAC90, 6, hex);
	CHECK(strcmp(hex, "51535556 8BD9") != 0 && strcmp(hex, "515355568BD9") == 0, "挂钩前入口字节 %s", hex);
	CHECK(Toggle(shm, TGL_MAX_HERO_LEVEL, true, 12345), "打开（等级 12345）");
	Hex(0xAC90, 6, hex);
	CHECK(hex[0] == 'E' && hex[1] == '9' && strcmp(hex + 10, "90") == 0, "入口已改为 JMP：%s", hex);
	DWORD_PTR value = 0;
	SendMessageTimeoutW(hwnd, WM_APP + 77, 0, 0, SMTO_ABORTIFHUNG, 3000, &value);
	CHECK(value == 12345, "调用被挂钩的 GetMiscInt(\"Misc\", \"MaxHeroLevel\") 返回 %lu", (unsigned long)value);
	CHECK(Toggle(shm, TGL_MAX_HERO_LEVEL, true, 99999), "运行中修改等级为 99999");
	SendMessageTimeoutW(hwnd, WM_APP + 77, 0, 0, SMTO_ABORTIFHUNG, 3000, &value);
	CHECK(value == 99999, "修改后返回 %lu", (unsigned long)value);
	CHECK(Toggle(shm, TGL_MAX_HERO_LEVEL, false, 0), "关闭");
	Hex(0xAC90, 6, hex);
	CHECK(strcmp(hex, "515355568BD9") == 0, "入口字节已还原 %s", hex);

	printf("后台线程还原（暂停游戏线程后写入）：\n");
	CHECK(Toggle(shm, TGL_AURA_STACK, true, 0), "游戏线程打开光环叠加");
	CheckSites(TGL_AURA_STACK, true);
	SendMessageW(hwnd, WM_APP + 78, 0, 0);      // 停止每帧：之后的开关由后台线程处理
	for (int t = 0; t < 2000 && shm->frameActive; t += 50) Sleep(50);
	CHECK(!shm->frameActive, "停止每帧");
	CHECK(Toggle(shm, TGL_AURA_STACK, false, 0), "后台线程关闭光环叠加");
	CheckSites(TGL_AURA_STACK, false);
	CHECK(IsWindow(hwnd) && SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 2000, NULL), "模拟进程主线程已恢复运行（没有被挂起）");
	SendMessageW(hwnd, WM_APP + 78, 1, 0);
	for (int t = 0; t < 2000 && !shm->frameActive; t += 50) Sleep(50);

	printf("卸载时自动还原：\n");
	CHECK(Toggle(shm, TGL_FUN_MODE, true, 0), "打开娱乐模式");
	GameLink_Shutdown(true);
	Sleep(800);
	CheckSites(TGL_FUN_MODE, false);
	Hex(0x4D3E30, 6, hook);
	CHECK(strcmp(hook, "890DF0ECAC6F") == 0, "每帧挂钩点已还原 %s", hook);
	SendMessageW(hwnd, WM_APP + 78, 0, 0);

	printf(Failures ? "\n共 %d 项失败\n" : "\n全部通过\n", Failures);
	return Failures ? 1 : 0;
}
