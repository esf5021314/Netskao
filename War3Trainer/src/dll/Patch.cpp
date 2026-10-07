// 模块说明：常驻开关的代码补丁，见 Patch.h
//
// 补丁数据来源：
//   原版 war3 1.24E修改器V1.6by大象 内嵌的 CE 脚本（不会失败 / 娱乐模式）
//   原版 war3 1.20e修改器V3.04by大象 内嵌的 CE 脚本（不会失败 / 娱乐模式 / 允许光环叠加 / 英雄最大等级10W）
// 1.20E 独有的功能移植到 1.24E 时，用特征码在 1.24E 的 Game.dll 中重新定位，并逐条反汇编核对。
#include "stdafx.h"
#include "Patch.h"
#include "Offsets.h"
#include "../common/Shared.h"
#include <stdarg.h>

// 一处补丁
struct PatchSite {
	DWORD		build;		// 适用版本
	int			toggleId;	// 所属开关
	DWORD		rva;		// 相对 Game.dll 的偏移
	const char*	origHex;	// 原始字节（写入前校验）
	const char*	newHex;		// 补丁字节
};

static const PatchSite kPatchSites[] = {
	// ================= 1.24E (6387) =================
	// 不会失败（原版 Ctrl+numeric 3 “NoDefeat”）：JASS 原生函数 IsNoDefeatCheat 恒返回 1
	//   0x3BD430: mov ecx,0D  ->  xor eax,eax / inc eax / ret / nop
	{ 6387, TGL_NO_DEFEAT,  0x3BD430, "B9 0D 00 00 00",       "33 C0 40 C3 90" },

	// 娱乐模式（原版 Ctrl+numeric 7，四段脚本）
	//   0x29C2DE: 单位数据取建造时间（UnitData+0x18，JASS GetUnitBuildTime 的实现）恒为 1 —— 快速建造 / 训练
	//             原版用 5 字节 JMP 跳到代码洞执行 mov eax,1 / pop ecx / ret，这里原地改写为等效的 5 字节
	{ 6387, TGL_FUN_MODE,   0x29C2DE, "8B 40 18 59 C3",       "33 C0 40 59 C3" },
	//   0x29CDDE: 单位数据字段 +0x1B0 恒为 4（原版娱乐模式的组成部分）
	{ 6387, TGL_FUN_MODE,   0x29CDDE, "8B 80 B0 01 00 00",    "B8 04 00 00 00 90" },
	//   0x00AF71: 人口上限 300 -> 65535；0x8882FC: 人口上限表 100 -> 65535（位于只读数据段）
	{ 6387, TGL_FUN_MODE,   0x00AF71, "7E 1D B8 2C 01 00 00", "90 90 B8 FF FF 00 00" },
	{ 6387, TGL_FUN_MODE,   0x8882FC, "64 00 00 00",          "FF FF 00 00" },
	//   0x2964E6 / 0x296533 / 0x296580: 建筑放置检查（含 FalseObstacle 判断）返回错误码 0xBA / 0x44 / 0x43
	//   的三个出口改跳到 0x2965BF（xor eax,eax）—— 建筑可以随意放置、可以重叠
	{ 6387, TGL_FUN_MODE,   0x2964E6, "E9 D6 00 00 00",       "E9 D4 00 00 00" },
	{ 6387, TGL_FUN_MODE,   0x296533, "E9 89 00 00 00",       "E9 87 00 00 00" },
	{ 6387, TGL_FUN_MODE,   0x296580, "EB 3F",                "EB 3D" },

	// 允许光环叠加（1.20E 独有，移植）：已有同类 Buff 时不再“刷新”，而是总是新加一个
	//   特征码 6A 01 6A 01 6A 01 6A 00 50 8B CB E8 ?? ?? ?? ?? 85 C0 8B CE 75 ?? 在 1.24E 唯一命中 0x0445C9，
	//   前后虚函数调用偏移 0x344 / 0x350 / 0x348 与 1.20E 完全一致
	{ 6387, TGL_AURA_STACK, 0x0445DD, "75 1E",                "90 90" },

	// ================= 1.20E (6074) =================
	{ 6074, TGL_NO_DEFEAT,  0x2D436F, "33 C0 C3 90 90",       "33 C0 40 C3 90" },
	{ 6074, TGL_FUN_MODE,   0x1A6CA9, "8B 42 18",             "33 C0 40" },
	{ 6074, TGL_FUN_MODE,   0x1A88C9, "8B 82 B0 01 00 00",    "B8 04 00 00 00 90" },
	{ 6074, TGL_FUN_MODE,   0x008CA6, "7E 1E B8 2C 01 00 00", "90 90 B8 FF FF 00 00" },
	{ 6074, TGL_FUN_MODE,   0x70636C, "64 00 00 00",          "FF FF 00 00" },
	{ 6074, TGL_FUN_MODE,   0x1B39CE, "B8 44 00 00 00",       "B8 00 00 00 00" },
	{ 6074, TGL_FUN_MODE,   0x1B3A9B, "B8 43 00 00 00",       "B8 00 00 00 00" },
	{ 6074, TGL_AURA_STACK, 0x43EFA9, "75 1B",                "90 90" },
};
static const int kPatchSiteCount = sizeof(kPatchSites) / sizeof(kPatchSites[0]);

static bool ToggleEnabled[W3T_TOGGLE_SLOTS];

// 写入失败原因（保证以 0 结尾）
static void ReasonSet(wchar_t* reason, int reasonSize, const wchar_t* format, ...) {
	if (!reason || reasonSize <= 0) return;
	va_list args;
	va_start(args, format);
	_vsnwprintf(reason, reasonSize - 1, format, args);
	va_end(args);
	reason[reasonSize - 1] = 0;
}

// "75 1E" -> {0x75, 0x1E}
static int HexParse(const char* hex, BYTE* out, int outSize) {
	int n = 0;
	while (*hex && n < outSize) {
		while (*hex == ' ') ++hex;
		if (!hex[0] || !hex[1]) break;
		char buf[3] = { hex[0], hex[1], 0 };
		out[n++] = (BYTE)strtoul(buf, NULL, 16);
		hex += 2;
	}
	return n;
}

static bool MemoryWrite(void* address, const BYTE* data, int size) {
	DWORD oldProtect;
	if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
	memcpy(address, data, size);
	VirtualProtect(address, size, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), address, size);
	return true;
}

static bool MemoryEqual(const void* address, const BYTE* data, int size) {
	if (IsBadReadPtr(address, size)) return false;
	return memcmp(address, data, size) == 0;
}

// ---------------------------------------------------------------------------
// 英雄最大等级（1.20E 独有“英雄最大等级10W”，移植到 1.24E）
//
// 1.24E 中 MaxHeroLevel 的 9 处引用全部是：
//     mov edx, "MaxHeroLevel" ; mov ecx, "Misc" ; push 默认值 ; call GetMiscInt(0xAC90)
// 因此只需挂钩 GetMiscInt 入口：键名为 "MaxHeroLevel" 时直接返回设定值。
// 入口前 6 字节 push ecx / push ebx / push ebp / push esi / mov ebx,ecx 正好在指令边界上。
//
// 存根放在 VirtualAlloc 的独立内存里，数值也存在存根内，卸载模块后即使别的线程
// 正在执行存根也不会访问已释放的模块内存；存根内存不释放（64 字节）。
// ---------------------------------------------------------------------------
static BYTE* MaxLevelStub = NULL;
static const BYTE kMiscGetIntOrig[6] = { 0x51, 0x53, 0x55, 0x56, 0x8B, 0xD9 };

static bool MaxLevelSupported() {
	return Offset(GAME_MISC_GET_INT) && Offset(STR_MISC_MAXHEROLEVEL);
}

static bool MaxLevelApply(bool enable, int level, wchar_t* reason, int reasonSize) {
	BYTE* target = static_cast<BYTE*>(Offset(GAME_MISC_GET_INT));
	DWORD key = reinterpret_cast<DWORD>(Offset(STR_MISC_MAXHEROLEVEL));
	if (!target || !key) {
		ReasonSet(reason, reasonSize, L"当前版本没有英雄最大等级的挂钩数据");
		return false;
	}
	if (!enable && !MaxLevelStub) return true;	// 从未挂过，无需还原
	if (!MaxLevelStub) {
		MaxLevelStub = static_cast<BYTE*>(VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		if (!MaxLevelStub) {
			ReasonSet(reason, reasonSize, L"申请存根内存失败");
			return false;
		}
		BYTE* p = MaxLevelStub;
		DWORD valueAddr = reinterpret_cast<DWORD>(MaxLevelStub + 0x30);
		*p++ = 0x81; *p++ = 0xFA; memcpy(p, &key, 4); p += 4;		// cmp edx, "MaxHeroLevel"
		*p++ = 0x75; *p++ = 0x08;									// jne 原始代码
		*p++ = 0xA1; memcpy(p, &valueAddr, 4); p += 4;				// mov eax, [数值]
		*p++ = 0xC2; *p++ = 0x04; *p++ = 0x00;						// ret 4
		memcpy(p, kMiscGetIntOrig, 6); p += 6;						// 被覆盖的 6 字节
		*p++ = 0xE9;												// jmp GetMiscInt+6
		DWORD rel = (DWORD)(target + 6) - (DWORD)(p + 4);
		memcpy(p, &rel, 4);
	}
	*reinterpret_cast<volatile LONG*>(MaxLevelStub + 0x30) = level > 0 ? level : 100000;

	BYTE jump[6];
	jump[0] = 0xE9;
	DWORD rel = (DWORD)MaxLevelStub - (DWORD)(target + 5);
	memcpy(jump + 1, &rel, 4);
	jump[5] = 0x90;

	if (enable) {
		if (MemoryEqual(target, jump, 6)) return true;		// 已经挂上，只更新数值
		if (!MemoryEqual(target, kMiscGetIntOrig, 6)) {
			ReasonSet(reason, reasonSize, L"GetMiscInt 入口字节与原版不一致（可能被其它插件修改），已拒绝写入");
			return false;
		}
		return MemoryWrite(target, jump, 6);
	}
	if (MemoryEqual(target, jump, 6)) return MemoryWrite(target, kMiscGetIntOrig, 6);
	return true;
}

// ---------------------------------------------------------------------------

bool PatchSupported(int toggleId) {
	if (toggleId == TGL_MAX_HERO_LEVEL) return MaxLevelSupported();
	DWORD build = Offset_GameVersion();
	for (int i = 0; i < kPatchSiteCount; ++i) {
		if (kPatchSites[i].build == build && kPatchSites[i].toggleId == toggleId) return true;
	}
	return false;
}

bool PatchIsEnabled(int toggleId) {
	return toggleId >= 0 && toggleId < W3T_TOGGLE_SLOTS && ToggleEnabled[toggleId];
}

bool PatchApply(int toggleId, bool enable, int argument, wchar_t* reason, int reasonSize) {
	reason[0] = 0;
	if (toggleId < 0 || toggleId >= W3T_TOGGLE_SLOTS) return false;

	if (toggleId == TGL_MAX_HERO_LEVEL) {
		if (!MaxLevelApply(enable, argument, reason, reasonSize)) return false;
		ToggleEnabled[toggleId] = enable;
		return true;
	}

	DWORD build = Offset_GameVersion();
	BYTE* base = reinterpret_cast<BYTE*>(Offset_GameBase());
	BYTE orig[16], patched[16];

	// 第一遍：全部校验，任何一处不符就整体放弃（不留下半开半关的状态）
	int count = 0;
	for (int i = 0; i < kPatchSiteCount; ++i) {
		const PatchSite& site = kPatchSites[i];
		if (site.build != build || site.toggleId != toggleId) continue;
		int n1 = HexParse(site.origHex, orig, sizeof(orig));
		int n2 = HexParse(site.newHex, patched, sizeof(patched));
		if (n1 != n2 || n1 == 0) return false;
		BYTE* addr = base + site.rva;
		const BYTE* expect = enable ? orig : patched;
		const BYTE* done = enable ? patched : orig;
		if (!MemoryEqual(addr, expect, n1) && !MemoryEqual(addr, done, n1)) {
			ReasonSet(reason, reasonSize, L"Game.dll+%X 处字节与原版不一致（可能被汉化补丁或其它插件修改），已拒绝写入", site.rva);
			return false;
		}
		++count;
	}
	if (count == 0) {
		ReasonSet(reason, reasonSize, L"当前版本没有该功能的补丁数据");
		return false;
	}

	// 第二遍：写入
	for (int i = 0; i < kPatchSiteCount; ++i) {
		const PatchSite& site = kPatchSites[i];
		if (site.build != build || site.toggleId != toggleId) continue;
		int n = HexParse(site.origHex, orig, sizeof(orig));
		HexParse(site.newHex, patched, sizeof(patched));
		BYTE* addr = base + site.rva;
		const BYTE* done = enable ? patched : orig;
		if (MemoryEqual(addr, done, n)) continue;
		if (!MemoryWrite(addr, done, n)) {
			ReasonSet(reason, reasonSize, L"写入 Game.dll+%X 失败（错误码 %lu）", site.rva, GetLastError());
			return false;
		}
	}
	ToggleEnabled[toggleId] = enable;
	return true;
}

void PatchRestoreAll() {
	wchar_t reason[160];
	for (int i = 0; i < W3T_TOGGLE_SLOTS; ++i) {
		if (ToggleEnabled[i]) PatchApply(i, false, 0, reason, 160);
	}
}
