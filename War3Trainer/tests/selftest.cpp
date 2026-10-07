// 模块说明：
// 自测程序（控制台），不需要游戏即可运行，用于检查：
//   1. SafeInvoke 能拦截游戏函数里的访问违例，执行全局展开（通知游戏自己的 SEH 帧），并正确恢复 fs:[0]；
//   2. 所有默认快捷键都能解析，并且格式化后再解析结果不变（包括没有名字、保存为 KeyXX 的键）；
//   3. 偏移表：17 个版本的原生函数表完整，1.24E / 1.20E 的地址与原版 CE 脚本一致；
//   4. 4 字符代码换算与原版脚本 `push 41496e76 //物品栏英雄` 一致。
// 编译：见 build_mingw.sh（生成 build/selftest.exe），在 Windows 或 Wine 下运行。
#include "../src/dll/stdafx.h"
#include "../src/dll/SafeCall.h"
#include "../src/dll/Offsets.h"
#include "../src/dll/Jass.h"
#include "../src/gui/Hotkey.h"
#include "../src/gui/Rows.h"
#include <stdio.h>

static int Failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++Failures; printf("  [失败] " __VA_ARGS__); printf("\n"); } } while (0)

static void Crash(void*) {
	volatile int* p = NULL;
	*p = 1;
}

static bool NestedInnerResult = true;
static void NestedHandled(void*) {
	// 内层再嵌套一次保护：内层的异常不应影响外层
	NestedInnerResult = SafeInvoke(Crash, NULL);
}

static int Counter = 0;
static void Normal(void*) { ++Counter; }

// 模拟游戏函数自己注册的 SEH 帧（相当于游戏里的 __try / __finally）：
// SafeInvoke 拦截异常后必须先全局展开，这个帧的处理函数应以 EXCEPTION_UNWINDING 被调用一次
#ifndef EXCEPTION_UNWINDING
#define EXCEPTION_UNWINDING 0x2
#endif
struct InnerSehFrame {
	InnerSehFrame* prev;
	void* handler;
};
static int UnwindCalls = 0;
static EXCEPTION_DISPOSITION __cdecl InnerHandler(EXCEPTION_RECORD* record, void*, CONTEXT*, void*) {
	if (record->ExceptionFlags & EXCEPTION_UNWINDING) ++UnwindCalls;
	return ExceptionContinueSearch;
}
static void __attribute__((noinline)) InnerWithFrame(void*) {
	InnerSehFrame frame;
	__asm__ __volatile__("movl %%fs:0, %0" : "=r"(frame.prev));
	frame.handler = reinterpret_cast<void*>(InnerHandler);
	__asm__ __volatile__("movl %0, %%fs:0" : : "r"(&frame) : "memory");
	volatile int* p = NULL;
	*p = 1;
	__asm__ __volatile__("movl %0, %%fs:0" : : "r"(frame.prev) : "memory");
}

static void TestSafeCall() {
	printf("SafeInvoke ...\n");
	void* before;
	__asm__ __volatile__("movl %%fs:0, %0" : "=r"(before));
	CHECK(SafeInvoke(Normal, NULL) && Counter == 1, "正常调用返回 false");
	CHECK(!SafeInvoke(Crash, NULL), "访问违例没有被拦截");
	CHECK(SafeLastExceptionCode() == EXCEPTION_ACCESS_VIOLATION, "异常码 %08lX", SafeLastExceptionCode());
	CHECK(SafeInvoke(NestedHandled, NULL) && !NestedInnerResult, "嵌套保护失败");
	UnwindCalls = 0;
	CHECK(!SafeInvoke(InnerWithFrame, NULL), "内层带 SEH 帧的异常没有被拦截");
	CHECK(UnwindCalls == 1, "全局展开没有通知内层 SEH 帧（%d 次）", UnwindCalls);
	for (int i = 0; i < 1000; ++i) SafeInvoke(Crash, NULL);
	for (int i = 0; i < 1000; ++i) SafeInvoke(InnerWithFrame, NULL);
	CHECK(UnwindCalls == 1001, "反复展开后计数 %d", UnwindCalls);
	void* after;
	__asm__ __volatile__("movl %%fs:0, %0" : "=r"(after));
	CHECK(before == after, "fs:[0] 没有恢复 %p -> %p", before, after);
	int x = 0;
	CHECK(SafeRun([&] { x = 42; }) && x == 42, "SafeRun lambda");
}

static void TestHotkeys() {
	printf("快捷键 ...\n");
	for (int i = 0; i < kRowCount; ++i) {
		HotkeyBinding b;
		CHECK(Hotkey_Parse(kRows[i].hotkey, b), "无法解析默认快捷键 %ls", kRows[i].hotkey);
		wchar_t text[64];
		Hotkey_Format(b, text, 64);
		HotkeyBinding b2;
		CHECK(Hotkey_Parse(text, b2) && Hotkey_Equal(b, b2), "格式化后不一致 %ls -> %ls", kRows[i].hotkey, text);
		CHECK(Hotkey_Equal(b, Hotkey_Unpack(Hotkey_Pack(b))), "打包后不一致 %ls", kRows[i].hotkey);
		for (int j = 0; j < i; ++j) {
			HotkeyBinding o;
			Hotkey_Parse(kRows[j].hotkey, o);
			CHECK(!Hotkey_Equal(b, o), "默认快捷键冲突：%ls（%ls 与 %ls）", kRows[i].hotkey, kRows[i].name, kRows[j].name);
		}
	}
	// 原版写法
	HotkeyBinding a, b;
	CHECK(Hotkey_Parse(L"Ctrl+numeric 1", a) && Hotkey_Parse(L"Ctrl+Num1", b) && Hotkey_Equal(a, b), "Ctrl+numeric 1");
	CHECK(Hotkey_Parse(L"Down Arrow+numeric 9", a) && Hotkey_Parse(L"Down+Num9", b) && Hotkey_Equal(a, b), "Down Arrow+numeric 9");
	CHECK(Hotkey_Parse(L"Ctrl+numeric *", a) && a.keys[0] == VK_MULTIPLY, "Ctrl+numeric *");
	CHECK(Hotkey_Parse(L"ctrl + shift + k", a) && a.mods == (HK_CTRL | HK_SHIFT) && a.keys[0] == 'K', "大小写 / 空格");
	CHECK(Hotkey_Parse(L"", a) && a.count == 0, "空字符串应为未设置");
	CHECK(!Hotkey_Parse(L"Ctrl+", a), "只有修饰键应解析失败");
	CHECK(!Hotkey_Parse(L"Ctrl+Foo", a), "未知键应解析失败");
	wchar_t text[64];
	HotkeyBinding add = { HK_CTRL, 1, { VK_ADD, 0, 0 } };
	Hotkey_Format(add, text, 64);
	CHECK(Hotkey_Parse(text, a) && Hotkey_Equal(a, add), "小键盘加号往返：%ls", text);
	// 所有虚拟键（包括没有名字、保存为 KeyXX 的键）格式化后都能解析回来，带不带修饰键都一样
	static const BYTE kMods[] = { 0, HK_CTRL, HK_CTRL | HK_ALT | HK_SHIFT };
	int bad = 0;
	for (int vk = 1; vk <= 0xFE; ++vk) {
		for (int m = 0; m < 3; ++m) {
			HotkeyBinding k = { kMods[m], 1, { (BYTE)vk, 0, 0 } };
			Hotkey_Format(k, text, 64);
			HotkeyBinding r;
			if (!Hotkey_Parse(text, r) || !Hotkey_Equal(k, r)) {
				if (bad++ < 5) printf("  [失败] 虚拟键 %02X 格式化为 %ls 后无法解析回来\n", vk, text);
			}
		}
	}
	CHECK(bad == 0, "共 %d 个虚拟键无法往返", bad);
	CHECK(Hotkey_Parse(L"Ctrl+KeyC1", a) && a.keys[0] == 0xC1 && a.mods == HK_CTRL, "KeyXX 写法");
	CHECK(!Hotkey_Parse(L"Key", a) && !Hotkey_Parse(L"KeyFF", a) && !Hotkey_Parse(L"Key0x", a) && !Hotkey_Parse(L"Key123", a), "无效的 KeyXX 应解析失败");
}

static void TestOffsets() {
	printf("偏移表 ...\n");
	static const DWORD kBuilds[] = { 6048, 6074, 6263, 6300, 6328, 6352, 6372, 6374, 6378, 6384, 6387, 6397, 6401, 52240, 7085, 7395, 7680 };
	for (size_t i = 0; i < sizeof(kBuilds) / sizeof(kBuilds[0]); ++i) {
		Offset_Init(kBuilds[i], 0x6F000000);
		int missing = 0;
		// NativesComplete 有缓存，这里直接逐个检查
		bool complete = true;
		for (int n = 0; n < NATIVE_ID_COUNT; ++n) if (!Offset(NATIVE_ID_FIRST + n)) { complete = false; missing = NATIVE_ID_FIRST + n; break; }
		CHECK(complete, "版本 %lu 缺少原生函数编号 %d", kBuilds[i], missing);
		CHECK(Offset(GLOBAL_GAMEUI) && Offset(GLOBAL_GAMEWAR3), "版本 %lu 缺少全局对象地址", kBuilds[i]);
	}
	// 1.24E：与原版 CE 脚本逐条比对
	Offset_Init(6387, 0x6F000000);
	struct { int id; DWORD rva; const char* name; } k124e[] = {
		{ NATIVE_CreateGroup, 0x3d3440, "CreateGroup" }, { NATIVE_FogEnable, 0x3bbd20, "FogEnable" },
		{ NATIVE_FogMaskEnable, 0x3bbd50, "FogMaskEnable" }, { NATIVE_Player, 0x3bc670, "Player" },
		{ NATIVE_GetLocalPlayer, 0x3bc6a0, "GetLocalPlayer" }, { NATIVE_GetOwningPlayer, 0x3c8cd0, "GetOwningPlayer" },
		{ NATIVE_SetPlayerAlliance, 0x3c1b90, "SetPlayerAlliance" }, { NATIVE_GetPlayerState, 0x3ca640, "GetPlayerState" },
		{ NATIVE_SetPlayerState, 0x3ca5e0, "SetPlayerState" }, { NATIVE_SetPlayerTechResearched, 0x3ca270, "SetPlayerTechResearched" },
		{ NATIVE_CreateUnit, 0x3c5d70, "CreateUnit" }, { NATIVE_FirstOfGroup, 0x3c4fa0, "FirstOfGroup" },
		{ NATIVE_GetUnitTypeId, 0x3c6450, "GetUnitTypeId" }, { NATIVE_GroupEnumUnitsSelected, 0x3cf0d0, "GroupEnumUnitsSelected" },
		{ NATIVE_KillUnit, 0x3c8b80, "KillUnit" }, { NATIVE_SetUnitInvulnerable, 0x3c7e30, "SetUnitInvulnerable" },
		{ NATIVE_SetUnitPosition, 0x3c6590, "SetUnitPosition" }, { NATIVE_SetUnitScale, 0x3c6e10, "SetUnitScale" },
		{ NATIVE_GetHeroLevel, 0x3c7a10, "GetHeroLevel" }, { NATIVE_SetHeroLevel, 0x3c78b0, "SetHeroLevel" },
		{ NATIVE_CreateItem, 0x3bc4e0, "CreateItem" }, { NATIVE_GetItemTypeId, 0x3c57a0, "GetItemTypeId" },
		{ NATIVE_SetItemCharges, 0x3c5bb0, "SetItemCharges" }, { NATIVE_UnitItemInSlot, 0x3c8270, "UnitItemInSlot" },
		{ NATIVE_UnitRemoveItemFromSlot, 0x3c81e0, "UnitRemoveItemFromSlot" }, { NATIVE_UnitAddItemToSlotById, 0x3c80f0, "UnitAddItemToSlotById" },
		{ NATIVE_UnitAddAbility, 0x3c8de0, "UnitAddAbility" }, { NATIVE_SetUnitAbilityLevel, 0x3c7cf0, "SetUnitAbilityLevel" },
		{ NATIVE_UnitRemoveAbility, 0x3c8e50, "UnitRemoveAbility" }, { NATIVE_UnitResetCooldown, 0x3c9210, "UnitResetCooldown" },
		{ UNIT_FROM_HANDLE, 0x3be7f0, "InGame_GetUnitAddress" }, { UNIT_ADD_ABILITY_INTERNAL, 0x24d900, "InGame_UnitAddAbitily" },
		{ GLOBAL_GAMEWAR3, 0xacd44c, "CGameWar3 全局" },
	};
	for (size_t i = 0; i < sizeof(k124e) / sizeof(k124e[0]); ++i) {
		CHECK(OffsetRva(k124e[i].id) == k124e[i].rva, "1.24E %s: %06lX != 原版 %06lX", k124e[i].name, OffsetRva(k124e[i].id), k124e[i].rva);
	}
	CHECK(Offset(NATIVE_CreateUnit) == (void*)(0x6F000000 + 0x3c5d70), "Offset() 绝对地址计算错误");
	// 1.20E：与原版 CE 脚本逐条比对
	Offset_Init(6074, 0x6F000000);
	struct { int id; DWORD rva; const char* name; } k120e[] = {
		{ NATIVE_CreateGroup, 0x2b93a0, "CreateGroup" }, { NATIVE_FogEnable, 0x2b2250, "FogEnable" },
		{ NATIVE_Player, 0x2cc9d0, "Player" }, { NATIVE_GetLocalPlayer, 0x2cca00, "GetLocalPlayer" },
		{ NATIVE_SetPlayerAlliance, 0x2ae920, "SetPlayerAlliance" }, { NATIVE_SetPlayerState, 0x2ce440, "SetPlayerState" },
		{ NATIVE_CreateUnit, 0x2bef80, "CreateUnit" }, { NATIVE_GroupEnumUnitsSelected, 0x2babd0, "GroupEnumUnitsSelected" },
		{ NATIVE_KillUnit, 0x2c79b0, "KillUnit" }, { NATIVE_SetHeroLevel, 0x2c39e0, "SetHeroLevel" },
		{ NATIVE_CreateItem, 0x2bd4b0, "CreateItem" }, { NATIVE_UnitAddAbility, 0x2c84e0, "UnitAddAbility" },
		{ NATIVE_UnitResetCooldown, 0x2c9410, "UnitResetCooldown" }, { UNIT_ADD_ABILITY_INTERNAL, 0x5cc280, "InGame_UnitAddAbitily" },
		{ GLOBAL_GAMEWAR3, 0x8722bc, "CGameWar3 全局" },
	};
	for (size_t i = 0; i < sizeof(k120e) / sizeof(k120e[0]); ++i) {
		CHECK(OffsetRva(k120e[i].id) == k120e[i].rva, "1.20E %s: %06lX != 原版 %06lX", k120e[i].name, OffsetRva(k120e[i].id), k120e[i].rva);
	}
	Offset_Init(9999, 0x6F000000);
	CHECK(Offset(NATIVE_CreateUnit) == NULL, "未知版本应返回 NULL");
}

static void TestFourCC() {
	printf("4字符代码 ...\n");
	CHECK(W3T_FourCC("AInv") == 0x41496e76, "AInv");
	CHECK(W3T_FourCC("srtl") == 0x7372746c, "srtl");
	CHECK(W3T_FourCC("AHab") == 0x41486162, "AHab");
	wchar_t text[5];
	W3T_FourCCToText(0x6b6c6d6d, text);
	CHECK(wcscmp(text, L"klmm") == 0, "klmm -> %ls", text);
}

int main() {
	SetConsoleOutputCP(CP_UTF8);
	TestSafeCall();
	TestHotkeys();
	TestOffsets();
	TestFourCC();
	printf(Failures ? "\n共 %d 项失败\n" : "\n全部通过\n", Failures);
	return Failures ? 1 : 0;
}
