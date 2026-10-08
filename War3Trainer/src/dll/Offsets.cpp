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

	// 人口上限缓存（娱乐模式的人口部分即时生效 / 即时还原）
	// 1.24E：GetFoodCeiling(0xAF10) 开头 mov eax,[0xA6575C] / cmp eax,-1 / jne 返回缓存值；
	//        地图读取时由 0xAE60 重置为 -1，玩家初始化（0x3AF93B / 0x3B1485）把它复制到每个玩家
	// 1.20E：同一函数（0x8C40 起）把结果写入 [0x7E7838]
	OffsetSet(GLOBAL_FOOD_CEILING_CACHE, 6074, 0x7E7838);
	OffsetSet(GLOBAL_FOOD_CEILING_CACHE, 6387, 0xA6575C);
	// GetFoodCeiling 本身（无参数，eax 返回）：用于在代码还原后按原版规则重新计算人口上限
	OffsetSet(GAME_FOOD_CEILING_GET, 6074, 0x008C40);
	OffsetSet(GAME_FOOD_CEILING_GET, 6387, 0x00AF10);

	// 每帧挂钩（原版 CE 脚本：1.24E game.dll+4D3E30，1.20E game.dll+4DD90）
	// 挂钩点是一个只有 mov [全局变量], ecx / ret 的小函数，全版本各有 3 处调用：
	//   CWorldFrameWar3 构造函数（ecx = 0）、开局初始化一次、CWorldFrameWar3 每帧函数（虚表第 11 项，
	//   1.24E 0x39CBD0：把本帧时间 [esi+23Ch] 累加到 [esi+390h] 后 call X / mov ecx,eax / call 挂钩点）。
	// 修改器只在“每帧函数”这一次调用（按返回地址区分）时执行，所以只在对局画面刷新时运行，
	// 时机与原版脚本相同，都在游戏线程里。
	// 各版本由 tools/re/w3re.py framehook 扫描得到（特征：每帧函数里 fstp [esi+390h] 之后的
	// mov ecx,eax / call <89 0D xx xx xx xx C3>，18 个版本均唯一命中）。
	//                      挂钩点                       返回地址                           写入的全局变量
	OffsetSet(GAME_FRAME_HOOK,  6048, 0x04DD90); OffsetSet(GAME_FRAME_HOOK_RETURN,  6048, 0x179C22); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6048, 0x85FF00);	// 1.20A
	OffsetSet(GAME_FRAME_HOOK,  6074, 0x04DD90); OffsetSet(GAME_FRAME_HOOK_RETURN,  6074, 0x179C32); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6074, 0x85FF20);	// 1.20E
	OffsetSet(GAME_FRAME_HOOK,  6263, 0x04DD90); OffsetSet(GAME_FRAME_HOOK_RETURN,  6263, 0x179FC2); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6263, 0x860F58);	// 1.21
	OffsetSet(GAME_FRAME_HOOK,  6300, 0x04DD90); OffsetSet(GAME_FRAME_HOOK_RETURN,  6300, 0x179FE2); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6300, 0x860F58);	// 1.21B
	OffsetSet(GAME_FRAME_HOOK,  6328, 0x4C3690); OffsetSet(GAME_FRAME_HOOK_RETURN,  6328, 0x39B515); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6328, 0xAA4888);	// 1.22
	OffsetSet(GAME_FRAME_HOOK,  6352, 0x4C5420); OffsetSet(GAME_FRAME_HOOK_RETURN,  6352, 0x39CAC5); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6352, 0xABD6D8);	// 1.23
	OffsetSet(GAME_FRAME_HOOK,  6372, 0x4D3D10); OffsetSet(GAME_FRAME_HOOK_RETURN,  6372, 0x39CB85); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6372, 0xACECF0);	// 1.24A
	OffsetSet(GAME_FRAME_HOOK,  6374, 0x4D3DB0); OffsetSet(GAME_FRAME_HOOK_RETURN,  6374, 0x39CB85); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6374, 0xACECF0);	// 1.24B
	OffsetSet(GAME_FRAME_HOOK,  6378, 0x4D3D70); OffsetSet(GAME_FRAME_HOOK_RETURN,  6378, 0x39CB85); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6378, 0xACECF0);	// 1.24C
	OffsetSet(GAME_FRAME_HOOK,  6384, 0x4D3DD0); OffsetSet(GAME_FRAME_HOOK_RETURN,  6384, 0x39CBE5); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6384, 0xACECF0);	// 1.24D
	OffsetSet(GAME_FRAME_HOOK,  6387, 0x4D3E30); OffsetSet(GAME_FRAME_HOOK_RETURN,  6387, 0x39CC45); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6387, 0xACECF0);	// 1.24E
	OffsetSet(GAME_FRAME_HOOK,  6397, 0x4D3100); OffsetSet(GAME_FRAME_HOOK_RETURN,  6397, 0x39BED5); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6397, 0xAB7E98);	// 1.25
	OffsetSet(GAME_FRAME_HOOK,  6401, 0x4D3330); OffsetSet(GAME_FRAME_HOOK_RETURN,  6401, 0x39C105); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  6401, 0xAB7E98);	// 1.26
	OffsetSet(GAME_FRAME_HOOK, 52240, 0x186D60); OffsetSet(GAME_FRAME_HOOK_RETURN, 52240, 0x3684F8); OffsetSet(GLOBAL_FRAME_HOOK_VAR, 52240, 0xBE3D70);	// 1.27A
	OffsetSet(GAME_FRAME_HOOK,  7085, 0x1A4A80); OffsetSet(GAME_FRAME_HOOK_RETURN,  7085, 0x385C88); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  7085, 0xD682D8);	// 1.27B
	OffsetSet(GAME_FRAME_HOOK,  7205, 0x1AAF50); OffsetSet(GAME_FRAME_HOOK_RETURN,  7205, 0x38D5B8); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  7205, 0xD72C20);	// 1.28A
	OffsetSet(GAME_FRAME_HOOK,  7395, 0x1AD520); OffsetSet(GAME_FRAME_HOOK_RETURN,  7395, 0x38FD08); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  7395, 0xD77C78);	// 1.28
	OffsetSet(GAME_FRAME_HOOK,  7680, 0x1D7310); OffsetSet(GAME_FRAME_HOOK_RETURN,  7680, 0x3B9D98); OffsetSet(GLOBAL_FRAME_HOOK_VAR,  7680, 0xD30110);	// 1.28F
}

static void InitMap() {
	// ===== JASS 原生函数（tools/gen_offsets.py 生成）=====
#include "NativeOffsets.inc"

	// ===== 全局对象 / 内部函数（tools/gen_offsets.py 生成）=====
#include "GlobalOffsets.inc"

	// ===== 手工维护 =====
	InitManualOffsets();
}
