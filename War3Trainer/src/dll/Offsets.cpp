// 模块说明：
// 偏移表的存储与查询。结构与 TuringY 的 Offsets.cpp 相同（OffsetSet / Offset），
// 去掉了加密与 VMProtect，地址以明文 RVA 保存，方便核对与增补新版本。
//
// 地址储存说明：
//   OffsetSet(编号, 版本号, RVA)    RVA 为相对 Game.dll 基址的偏移
//   未登记的版本 Offset() 返回 NULL，调用方据此判断“该版本不支持”
#include "stdafx.h"
#include "Offsets.h"
#include <unordered_map>

typedef std::unordered_map<DWORD, DWORD> VersionAddressMapType;   // 版本号 -> RVA
typedef std::unordered_map<int, VersionAddressMapType> OffsetMapType;

static DWORD VersionId;
static DWORD Base;
static OffsetMapType OffsetMap;

static void InitMap();

bool Offset_Init(DWORD version, DWORD base) {
	VersionId = version;
	Base = base;
	OffsetMap.clear();
	InitMap();
	return true;
}

DWORD Offset_GameVersion() { return VersionId; }
DWORD Offset_GameBase() { return Base; }

DWORD OffsetRva(int offset) {
	OffsetMapType::const_iterator idIter = OffsetMap.find(offset);
	if (idIter == OffsetMap.end()) return 0;
	VersionAddressMapType::const_iterator verIter = idIter->second.find(VersionId);
	if (verIter == idIter->second.end()) return 0;
	return verIter->second;
}

void* Offset(int offset) {
	DWORD rva = OffsetRva(offset);
	return rva ? reinterpret_cast<void*>(Base + rva) : NULL;
}

static void OffsetSet(int offsetIdentifier, DWORD gameVersion, DWORD address) {
	if (address) OffsetMap[offsetIdentifier][gameVersion] = address;
}

// 手工维护的内部函数 / 数据地址
// 只登记逐条核对过的版本；其它版本留空，对应功能会自动回退到 JASS 原生函数或提示不支持
static void InitManualOffsets() {
	// CUnit 内部添加技能（原版 CE 脚本 InGame_UnitAddAbitily）
	// 1.24E：反汇编核对为 fastcall，两处出口均为 ret 0Ch
	OffsetSet(UNIT_ADD_ABILITY_INTERNAL, 6074, 0x5CC280);	// 1.20E（原版 CE 脚本）
	OffsetSet(UNIT_ADD_ABILITY_INTERNAL, 6387, 0x24D900);	// 1.24E（原版 CE 脚本 + 反汇编核对）

	// 物品数据哈希表（创建所有物品）
	// 1.20E：0x1D6CA4 处构造（[+0]=虚表，[+4]=0xDDDDDDDD 链表偏移初值，[+0xC]=首节点），
	//        原版 CE 脚本的 FirstItem = 870CCC 即 +0xC。TuringY 登记的 0x870CC4 偏了 4 字节。
	// 1.24E：0x2B9320 遍历函数读取 [ACBA30] 首节点、[ACBA28] 链表偏移，节点 +0x14 为物品 ID。
	OffsetSet(GLOBAL_ITEMDATA_TABLE, 6074, 0x870CC0);
	OffsetSet(GLOBAL_ITEMDATA_TABLE, 6387, 0xACBA24);

	// 读取 Misc 常量（英雄最大等级挂钩）
	// 1.24E：MaxHeroLevel 的 9 处引用全部是 mov edx,"MaxHeroLevel" / mov ecx,"Misc" / call 0xAC90
	OffsetSet(GAME_MISC_GET_INT, 6387, 0x00AC90);
	OffsetSet(STR_MISC_MAXHEROLEVEL, 6387, 0x888368);
}

static void InitMap() {
	// ===== JASS 原生函数（tools/gen_offsets.py 生成）=====
#include "NativeOffsets.inc"

	// ===== 全局对象 / 内部函数（tools/gen_offsets.py 生成）=====
#include "GlobalOffsets.inc"

	// ===== 手工维护 =====
	InitManualOffsets();
}
