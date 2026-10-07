// 模块说明：
// JASS 原生函数包装。所有原生函数都是 __cdecl，统一通过
//     aero::generic_c_call<返回类型>(Offset(NATIVE_xxx), 参数...)
// 调用（与 TuringY / 原版 CE 脚本 `push ...; call CreateUnit; add esp,14` 一致）。
//
// 参数约定（原版 CE 脚本与 TuringY JassNatives.prototype.inc 相同）：
//   * handle / integer / 常量类型（playerstate、alliancetype 等）都按 32 位整数传递，
//     常量类型直接传 Convert 之后的整数值，例如 PLAYER_STATE_RESOURCE_GOLD = 1；
//   * boolean 按 32 位整数传递（这里统一用 int，避免 bool 只写低 8 位）；
//   * real 参数传 float 指针（CE 脚本 `lea ebx,[MouseX]; push ebx`）；
//   * real 返回值在 EAX 中，是 float 的位模式。
//
// 调用前提：必须在游戏主线程、且处于游戏中（见 Tools.h 的 IsInGame）。
#ifndef JASS_H_INCLUDED_
#define JASS_H_INCLUDED_

#include "stdafx.h"
#include "Offsets.h"

namespace jass {

	typedef uint32_t handle;

	// ---- 常量（common.j）----
	enum {
		PLAYER_STATE_RESOURCE_GOLD = 1,
		PLAYER_STATE_RESOURCE_LUMBER = 2,
		PLAYER_STATE_FOOD_CAP_CEILING = 6,
		ALLIANCE_SHARED_CONTROL = 6,
		ALLIANCE_SHARED_ADVANCED_CONTROL = 7,
		UNIT_STATE_LIFE = 0,
		UNIT_STATE_MAX_LIFE = 1,
		UNIT_STATE_MANA = 2,
		UNIT_STATE_MAX_MANA = 3,
		UNIT_TYPE_HERO = 0,
		MAP_CONTROL_USER = 0,
		PLAYER_SLOT_STATE_PLAYING = 1,
		PLAYER_NEUTRAL_AGGRESSIVE = 12,
		PLAYER_NEUTRAL_PASSIVE = 15,
		MAX_PLAYER_SLOTS = 12           // 1.28 及以前的可用玩家数
	};

	inline float RealGet(uint32_t bits) { float f; memcpy(&f, &bits, sizeof(f)); return f; }

	// ---- 单位组 ----
	inline handle CreateGroup() { return aero::generic_c_call<handle>(Offset(NATIVE_CreateGroup)); }
	inline void DestroyGroup(handle g) { aero::generic_c_call<void>(Offset(NATIVE_DestroyGroup), g); }
	inline void GroupEnumUnitsSelected(handle g, handle p, handle filter) { aero::generic_c_call<void>(Offset(NATIVE_GroupEnumUnitsSelected), g, p, filter); }
	inline void GroupEnumUnitsOfPlayer(handle g, handle p, handle filter) { aero::generic_c_call<void>(Offset(NATIVE_GroupEnumUnitsOfPlayer), g, p, filter); }
	inline handle FirstOfGroup(handle g) { return aero::generic_c_call<handle>(Offset(NATIVE_FirstOfGroup), g); }
	inline void GroupRemoveUnit(handle g, handle u) { aero::generic_c_call<void>(Offset(NATIVE_GroupRemoveUnit), g, u); }

	// ---- 玩家 ----
	inline handle Player(int number) { return aero::generic_c_call<handle>(Offset(NATIVE_Player), number); }
	inline handle GetLocalPlayer() { return aero::generic_c_call<handle>(Offset(NATIVE_GetLocalPlayer)); }
	inline handle GetOwningPlayer(handle u) { return aero::generic_c_call<handle>(Offset(NATIVE_GetOwningPlayer), u); }
	inline int GetPlayerId(handle p) { return aero::generic_c_call<int>(Offset(NATIVE_GetPlayerId), p); }
	inline int GetPlayerController(handle p) { return aero::generic_c_call<int>(Offset(NATIVE_GetPlayerController), p); }
	inline int GetPlayerSlotState(handle p) { return aero::generic_c_call<int>(Offset(NATIVE_GetPlayerSlotState), p); }
	inline void SetPlayerAlliance(handle source, handle other, int allianceType, int value) { aero::generic_c_call<void>(Offset(NATIVE_SetPlayerAlliance), source, other, allianceType, value); }
	inline void SetPlayerState(handle p, int state, int value) { aero::generic_c_call<void>(Offset(NATIVE_SetPlayerState), p, state, value); }
	inline int GetPlayerState(handle p, int state) { return aero::generic_c_call<int>(Offset(NATIVE_GetPlayerState), p, state); }
	inline void SetPlayerTechResearched(handle p, int techId, int level) { aero::generic_c_call<void>(Offset(NATIVE_SetPlayerTechResearched), p, techId, level); }
	inline void SetPlayerHandicapXP(handle p, float rate) { aero::generic_c_call<void>(Offset(NATIVE_SetPlayerHandicapXP), p, &rate); }

	// ---- 单位 ----
	inline handle CreateUnit(handle p, int unitId, float x, float y, float face) { return aero::generic_c_call<handle>(Offset(NATIVE_CreateUnit), p, unitId, &x, &y, &face); }
	inline void KillUnit(handle u) { aero::generic_c_call<void>(Offset(NATIVE_KillUnit), u); }
	inline void RemoveUnit(handle u) { aero::generic_c_call<void>(Offset(NATIVE_RemoveUnit), u); }
	inline void SetUnitInvulnerable(handle u, int flag) { aero::generic_c_call<void>(Offset(NATIVE_SetUnitInvulnerable), u, flag); }
	inline void SetUnitPosition(handle u, float x, float y) { aero::generic_c_call<void>(Offset(NATIVE_SetUnitPosition), u, &x, &y); }
	inline void SetUnitScale(handle u, float x, float y, float z) { aero::generic_c_call<void>(Offset(NATIVE_SetUnitScale), u, &x, &y, &z); }
	inline int GetUnitTypeId(handle u) { return aero::generic_c_call<int>(Offset(NATIVE_GetUnitTypeId), u); }
	inline float GetUnitFacing(handle u) { return RealGet(aero::generic_c_call<uint32_t>(Offset(NATIVE_GetUnitFacing), u)); }
	inline float GetUnitX(handle u) { return RealGet(aero::generic_c_call<uint32_t>(Offset(NATIVE_GetUnitX), u)); }
	inline float GetUnitY(handle u) { return RealGet(aero::generic_c_call<uint32_t>(Offset(NATIVE_GetUnitY), u)); }
	inline float GetUnitState(handle u, int state) { return RealGet(aero::generic_c_call<uint32_t>(Offset(NATIVE_GetUnitState), u, state)); }
	inline void SetUnitState(handle u, int state, float value) { aero::generic_c_call<void>(Offset(NATIVE_SetUnitState), u, state, &value); }
	inline void SetUnitVertexColor(handle u, int r, int g, int b, int a) { aero::generic_c_call<void>(Offset(NATIVE_SetUnitVertexColor), u, r, g, b, a); }
	inline void SetUnitPathing(handle u, int flag) { aero::generic_c_call<void>(Offset(NATIVE_SetUnitPathing), u, flag); }
	inline void PauseUnit(handle u, int flag) { aero::generic_c_call<void>(Offset(NATIVE_PauseUnit), u, flag); }
	inline bool IsUnitPaused(handle u) { return aero::generic_c_call<int>(Offset(NATIVE_IsUnitPaused), u) != 0; }
	inline bool IsUnitType(handle u, int unitType) { return aero::generic_c_call<int>(Offset(NATIVE_IsUnitType), u, unitType) != 0; }
	inline bool IssueTargetOrderById(handle u, int order, handle target) { return aero::generic_c_call<int>(Offset(NATIVE_IssueTargetOrderById), u, order, target) != 0; }
	inline bool IssueImmediateOrderById(handle u, int order) { return aero::generic_c_call<int>(Offset(NATIVE_IssueImmediateOrderById), u, order) != 0; }
	inline int GetUnitCurrentOrder(handle u) { return aero::generic_c_call<int>(Offset(NATIVE_GetUnitCurrentOrder), u); }

	// ---- 英雄 ----
	inline int GetHeroLevel(handle u) { return aero::generic_c_call<int>(Offset(NATIVE_GetHeroLevel), u); }
	inline void SetHeroLevel(handle u, int level, int showEyeCandy) { aero::generic_c_call<void>(Offset(NATIVE_SetHeroLevel), u, level, showEyeCandy); }
	inline int GetHeroStr(handle u, int includeBonuses) { return aero::generic_c_call<int>(Offset(NATIVE_GetHeroStr), u, includeBonuses); }
	inline int GetHeroAgi(handle u, int includeBonuses) { return aero::generic_c_call<int>(Offset(NATIVE_GetHeroAgi), u, includeBonuses); }
	inline int GetHeroInt(handle u, int includeBonuses) { return aero::generic_c_call<int>(Offset(NATIVE_GetHeroInt), u, includeBonuses); }
	inline void SetHeroStr(handle u, int value, int permanent) { aero::generic_c_call<void>(Offset(NATIVE_SetHeroStr), u, value, permanent); }
	inline void SetHeroAgi(handle u, int value, int permanent) { aero::generic_c_call<void>(Offset(NATIVE_SetHeroAgi), u, value, permanent); }
	inline void SetHeroInt(handle u, int value, int permanent) { aero::generic_c_call<void>(Offset(NATIVE_SetHeroInt), u, value, permanent); }
	inline bool UnitModifySkillPoints(handle u, int delta) { return aero::generic_c_call<int>(Offset(NATIVE_UnitModifySkillPoints), u, delta) != 0; }

	// ---- 技能 ----
	inline bool UnitAddAbility(handle u, int abilityId) { return aero::generic_c_call<int>(Offset(NATIVE_UnitAddAbility), u, abilityId) != 0; }
	inline bool UnitRemoveAbility(handle u, int abilityId) { return aero::generic_c_call<int>(Offset(NATIVE_UnitRemoveAbility), u, abilityId) != 0; }
	inline int SetUnitAbilityLevel(handle u, int abilityId, int level) { return aero::generic_c_call<int>(Offset(NATIVE_SetUnitAbilityLevel), u, abilityId, level); }
	inline void UnitResetCooldown(handle u) { aero::generic_c_call<void>(Offset(NATIVE_UnitResetCooldown), u); }

	// ---- 物品 ----
	inline handle CreateItem(int itemId, float x, float y) { return aero::generic_c_call<handle>(Offset(NATIVE_CreateItem), itemId, &x, &y); }
	inline int GetItemTypeId(handle item) { return aero::generic_c_call<int>(Offset(NATIVE_GetItemTypeId), item); }
	inline void SetItemCharges(handle item, int charges) { aero::generic_c_call<void>(Offset(NATIVE_SetItemCharges), item, charges); }
	inline handle UnitItemInSlot(handle u, int slot) { return aero::generic_c_call<handle>(Offset(NATIVE_UnitItemInSlot), u, slot); }
	inline handle UnitRemoveItemFromSlot(handle u, int slot) { return aero::generic_c_call<handle>(Offset(NATIVE_UnitRemoveItemFromSlot), u, slot); }
	inline bool UnitAddItemToSlotById(handle u, int itemId, int slot) { return aero::generic_c_call<int>(Offset(NATIVE_UnitAddItemToSlotById), u, itemId, slot) != 0; }

	// ---- 游戏 ----
	inline void FogEnable(int enable) { aero::generic_c_call<void>(Offset(NATIVE_FogEnable), enable); }
	inline void FogMaskEnable(int enable) { aero::generic_c_call<void>(Offset(NATIVE_FogMaskEnable), enable); }
	inline void PauseGame(int flag) { aero::generic_c_call<void>(Offset(NATIVE_PauseGame), flag); }

	// 检查本工程用到的全部原生函数在当前版本是否都已登记；missing 返回第一个缺失的编号
	bool NativesComplete(int* missing);

}//namespace jass

#endif // JASS_H_INCLUDED_
