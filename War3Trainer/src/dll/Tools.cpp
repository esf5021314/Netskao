// 模块说明：工具函数实现，见 Tools.h
#include "stdafx.h"
#include "Tools.h"
#include "Jass.h"
#include <math.h>

// 地址是否可读（用于全局变量地址：版本表登记错误时不至于读到未映射的内存）
static bool AddressReadable(const void* address, size_t size) {
	MEMORY_BASIC_INFORMATION mbi;
	if (!address || !VirtualQuery(address, &mbi, sizeof(mbi))) return false;
	if (mbi.State != MEM_COMMIT) return false;
	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
	return (uintptr_t)address + size <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

war3::CGameUI* GameUIObjectGet() {
	war3::CGameUI** ref = reinterpret_cast<war3::CGameUI**>(Offset(GLOBAL_GAMEUI));
	return AddressReadable(ref, sizeof(*ref)) ? *ref : NULL;
}

war3::CGameWar3* GameObjectGet() {
	war3::CGameWar3** ref = reinterpret_cast<war3::CGameWar3**>(Offset(GLOBAL_GAMEWAR3));
	return AddressReadable(ref, sizeof(*ref)) ? *ref : NULL;
}

bool IsInGame() {
	// 旧版重制时踩过的坑：主菜单 / 选图界面 CreateGroup() 返回 0，再传给
	// GroupEnumUnitsSelected 会在 0x3A8BA3 处 cmp [ecx+1C] 访问空指针崩溃。
	// 这里要求游戏对象、界面对象、世界画面三者都已创建，才认为可以调用原生函数。
	war3::CGameWar3* game = GameObjectGet();
	if (!game) return false;
	war3::CGameUI* ui = GameUIObjectGet();
	if (!ui) return false;
	war3::CWorldFrameWar3* world = GameUIField(ui->world);
	if (!world) return false;
	uint16_t slot = game->localPlayerSlot;
	if (slot >= 16 || !game->players[slot]) return false;
	return true;
}

bool MouseWorldPosGet(float& x, float& y) {
	war3::CGameUI* ui = GameUIObjectGet();
	if (!ui) return false;
	war3::CWorldFrameWar3* world = GameUIField(ui->world);
	if (!world) return false;
	x = world->mouseWorldX;
	y = world->mouseWorldY;
	// 地图坐标最大约 ±16384（256x256 格），超出说明读到的不是坐标
	if (!(fabsf(x) < 65536.0f) || !(fabsf(y) < 65536.0f)) return false;
	return true;
}

void* UnitObjectGet(uint32_t unitHandle) {
	if (!unitHandle) return NULL;
	void* fn = Offset(UNIT_FROM_HANDLE);
	if (!fn) return NULL;
	if (GameVersionGet() < 6328) {
		// 6328 之前：thiscall(helper2(helper1(0)), handle)，与 TuringY UnitGetObject 相同
		void* h1 = Offset(UNIT_FROM_HANDLE_HELPER_1);
		void* h2 = Offset(UNIT_FROM_HANDLE_HELPER_2);
		if (!h1 || !h2) return NULL;
		void* helper = aero::generic_fast_call<void*>(h2, aero::generic_fast_call<void*>(h1, 0));
		return aero::generic_this_call<void*>(fn, helper, unitHandle);
	}
	// 6328 起：fastcall(handle)，即原版 CE 脚本 `mov ecx,[Unit]; call InGame_GetUnitAddress`
	return aero::generic_fast_call<void*>(fn, unitHandle);
}

void TextPrint(float duration, const char* utf8Text) {
	void* fn = Offset(GAMEUI_TEXT_DISPLAY);
	war3::CGameUI* ui = GameUIObjectGet();
	if (!fn || !ui || !utf8Text) return;
	// CGameUI::DisplayText(this, x, y, text, duration, -1)，与 TuringY OutputScreen 相同
	aero::generic_this_call<void>(fn, ui, 0.0f, 0.0f, utf8Text, duration, -1);
}

void WideToUtf8(const wchar_t* src, char* dst, int dstSize) {
	if (!dst || dstSize <= 0) return;
	dst[0] = 0;
	if (!src) return;
	int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dstSize, NULL, NULL);
	if (n <= 0) dst[0] = 0;
	dst[dstSize - 1] = 0;
}
