// 模块说明：命令执行层，见 Trainer.h
//
// 每个功能都对照原版 CE 脚本实现，注释里写明对应的脚本段落：
//   [124E+xxx] = war3 1.24E修改器V1.6by大象 脚本中 myhook_install+xxx 处的分支
//   [120E+xxx] = war3 1.20e修改器V3.04by大象 脚本中 myhook_install+xxx 处的分支
//   [127A]     = 魔兽争霸1.27终极版（易语言）功能清单中的项目，原版代码被混淆，按功能用 JASS 原生函数实现
//
// 与原版的差异：
//   * 原版用一个全局单位组反复使用，换图后单位组失效会导致“选不中单位”；
//     这里每次 CreateGroup / DestroyGroup，没有泄漏也不会失效。
//   * 原版复制单位用 Location + CreateUnitAtLoc，每次泄漏一个 location；这里直接用坐标。
//   * 所有游戏调用都包在 SafeRun 里，出错只让这一条命令失败。
//   * 检测到多人游戏（超过一个真人玩家）时拒绝执行：本修改器只用于单人游戏，
//     与游戏自带的作弊码（whosyourdaddy 等）适用范围一致，也避免多人游戏不同步掉线。
#include "stdafx.h"
#include "Trainer.h"
#include "Jass.h"
#include "Tools.h"
#include "Patch.h"
#include "SafeCall.h"
#include <vector>
#include <map>

using namespace jass;

// ---------------------------------------------------------------------------
// 4字符代码常量
// ---------------------------------------------------------------------------
static inline int FourCC(const char* s) { return (int)W3T_FourCC(s); }

// 全光环 [124E+0E20] / [120E+0C80]：最后一个“治疗守卫光环”原版没有设置等级
static const char* const kAuraCodes[] = { "AHab", "AHad", "AOr2", "AUau", "AUav", "AEar", "ACac" };
static const char* const kAuraNoLevel = "Aoar";
// 全被动 [124E+0F50] / [120E+0E00]：先加英雄物品栏，再加重击 / 致命一击 / 醉拳
static const char* const kPassiveCodes[] = { "AHbh", "AOcr", "Acdb" };
// 设置技能等级时使用的等级，原版 `push 70`（0x70 = 112，游戏会截断到最高级）
static const int kMaxAbilityLevel = 0x70;

// JASS 命令 ID（从 1.24E Game.dll 的命令注册表读出：push "innerfire" / push 0D0062 / call 0x3B1270）
static const int ORDER_INNERFIRE = 852066;      // 0x0D0062
static const int ORDER_BLOODLUST = 852101;      // 0x0D0085
static const int ORDER_UNHOLYFRENZY = 852209;   // 0x0D00F1
static const int ORDER_ROAR = 852164;           // 0x0D00C4
static const int ORDER_POLYMORPH = 852074;      // 0x0D006A

// ---------------------------------------------------------------------------
// 执行结果
// ---------------------------------------------------------------------------
struct CommandResult {
	int code;
	wchar_t text[160];
};

static void ResultSet(CommandResult& r, int code, const wchar_t* format, ...) {
	r.code = code;
	va_list args;
	va_start(args, format);
	_vsnwprintf(r.text, 159, format, args);
	va_end(args);
	r.text[159] = 0;
}

// ---------------------------------------------------------------------------
// 运行时状态
// ---------------------------------------------------------------------------
// 单位句柄会被游戏回收再分配给别的单位，所以记录句柄时同时记录单位对象和类型，
// 使用前核对，避免误删 / 误改另一个单位
struct UnitRef {
	handle unit;
	void* object;           // UnitObjectGet(unit)
	int typeId;
};
struct PendingRemoval {
	UnitRef ref;
	DWORD created;          // 创建时间
	DWORD deadline;         // 最长保留时间（施法完成后会提前删除）
};
static std::vector<PendingRemoval> DummyUnits;     // 等待删除的施法单位
static std::map<handle, UnitRef> NoCollisionUnits; // 已关闭碰撞体积的单位
static bool GamePausedByTrainer = false;
static DWORD LastSelectionRefresh = 0;
static bool MultiplayerWarned = false;
static void* LastGameObject = NULL;                // 用来发现“换图 / 重新开始”

// 娱乐模式的人口部分（见 FoodCeilingApply）
static bool FoodApplied = false;
static int FoodCacheSaved = -1;
static int FoodPlayerSaved[jass::MAX_PLAYER_SLOTS];
static bool FoodRecomputePending = false;          // 娱乐模式只还原了代码，下一次进入对局时要修正人口上限

// 常驻开关只能在单人游戏进行中开启，离开游戏自动关闭。
// 测试版模块（build_mingw.sh test 定义 W3T_TEST_BUILD，只给 tests/patchtest 用）运行在没有对局的
// 模拟进程里，放开这一限制，用来逐字节检查补丁的写入与还原。正式版本不定义此宏。
#ifdef W3T_TEST_BUILD
static const bool kTogglesNeedGame = false;
#else
static const bool kTogglesNeedGame = true;
#endif

// ---------------------------------------------------------------------------
// 基础操作
// ---------------------------------------------------------------------------

// 游戏中的真人玩家数（控制者为用户且槽位正在游戏）
static int HumanPlayersCount() {
	int count = 0;
	for (int i = 0; i < MAX_PLAYER_SLOTS; ++i) {
		handle p = Player(i);
		if (!p) continue;
		if (GetPlayerController(p) == MAP_CONTROL_USER && GetPlayerSlotState(p) == PLAYER_SLOT_STATE_PLAYING) ++count;
	}
	return count;
}

// 本地玩家当前选中的第一个单位 [124E mycall_GetSelectUnit]
static handle SelectedUnitGet() {
	handle group = CreateGroup();
	if (!group) return 0;
	GroupEnumUnitsSelected(group, GetLocalPlayer(), 0);
	handle unit = FirstOfGroup(group);
	DestroyGroup(group);
	return unit;
}

static void CodeText(int code, wchar_t out[5]) { W3T_FourCCToText((DWORD)code, out); }

static UnitRef UnitRefMake(handle unit) {
	UnitRef ref = { unit, UnitObjectGet(unit), GetUnitTypeId(unit) };
	return ref;
}

// 句柄仍然指向记录时的同一个单位
static bool UnitRefValid(const UnitRef& ref) {
	if (!ref.unit || !ref.object) return false;
	return UnitObjectGet(ref.unit) == ref.object && GetUnitTypeId(ref.unit) == ref.typeId;
}

// 只核对单位对象（变身 / 钻地等会改变单位类型，但仍是同一个单位）
static bool UnitRefSameObject(const UnitRef& ref) {
	return ref.unit && ref.object && UnitObjectGet(ref.unit) == ref.object;
}

// 内部函数添加技能（原版 InGame_UnitAddAbitily），可以重复添加同一技能
// 返回新建的 CAbility*；技能代码无效时返回 0（1.24E 0x24D92E: xor eax,eax / ret 0Ch）
static bool AbilityAddInternal(handle unit, int abilityId) {
	void* fn = Offset(UNIT_ADD_ABILITY_INTERNAL);
	if (!fn) return false;
	void* object = UnitObjectGet(unit);
	if (!object) return false;
	// [124E+0880]: push 0 / push 0 / push 0 / mov edx,技能ID / mov ecx,单位对象 / call InGame_UnitAddAbitily
	void* ability = aero::generic_fast_call<void*>(fn, object, abilityId, 0, 0, 0);
	return ability != NULL;
}

// 用隐形施法单位对目标施放一个技能（全BUFF / 变绵羊）
// abilityCodes 依次尝试（野怪版本没有科技需求，优先使用），dummyTypes 依次尝试（需要有魔法值的单位）
// IssueTargetOrderById 在“魔法不足 / 目标不合法 / 技能不可用”时同步返回 false，可据此换下一种组合
static bool CastWithDummy(handle owner, handle target, const char* const* abilityCodes, int codeCount,
	int orderId, bool immediate) {
	static const char* const kDummyTypes[] = { "hsor", "hmpr", "oshm", "unec", "edoc" };
	float x = GetUnitX(target);
	float y = GetUnitY(target);
	for (size_t t = 0; t < sizeof(kDummyTypes) / sizeof(kDummyTypes[0]); ++t) {
		handle dummy = CreateUnit(owner, FourCC(kDummyTypes[t]), x, y, 0.0f);
		if (!dummy) continue;
		SetUnitVertexColor(dummy, 255, 255, 255, 0);    // 完全透明
		UnitAddAbility(dummy, FourCC("Aloc"));          // 蝗虫：不可选中、不可攻击
		UnitRemoveAbility(dummy, FourCC("Aatk"));       // 去掉攻击，不会自动攻击附近的单位
		SetUnitPathing(dummy, 0);
		bool casterReady = false;                       // 技能加上了且有魔法值：失败只能是目标的原因
		for (int c = 0; c < codeCount; ++c) {
			int code = FourCC(abilityCodes[c]);
			if (!UnitAddAbility(dummy, code)) continue;
			SetUnitAbilityLevel(dummy, code, 1);
			SetUnitState(dummy, UNIT_STATE_MANA, GetUnitState(dummy, UNIT_STATE_MAX_MANA));
			if (GetUnitState(dummy, UNIT_STATE_MAX_MANA) > 0.0f) casterReady = true;
			bool issued = immediate ? IssueImmediateOrderById(dummy, orderId) : IssueTargetOrderById(dummy, orderId, target);
			if (issued) {
				PendingRemoval pr;
				pr.ref = UnitRefMake(dummy);
				pr.created = GetTickCount();
				pr.deadline = pr.created + 30000;
				DummyUnits.push_back(pr);
				return true;
			}
			UnitRemoveAbility(dummy, code);
		}
		RemoveUnit(dummy);
		if (casterReady) return false;      // 目标不合法（英雄 / 魔免等），换施法单位也没用
	}
	return false;
}

// 娱乐模式的人口部分即时生效 / 即时还原
// GetFoodCeiling 只在读图时计算一次并缓存（[GLOBAL_FOOD_CEILING_CACHE]），并复制到每个玩家的
// PLAYER_STATE_FOOD_CAP_CEILING；只改代码不会影响已经开始的游戏，所以这里同时改缓存和玩家状态。
static void FoodCeilingApply(bool enable) {
	int* cache = reinterpret_cast<int*>(Offset(GLOBAL_FOOD_CEILING_CACHE));
	if (enable) {
		FoodRecomputePending = false;
		if (FoodApplied) return;
		// 先全部保存再修改：中途出错时，已改动的部分也能在关闭时还原
		for (int i = 0; i < MAX_PLAYER_SLOTS; ++i) {
			FoodPlayerSaved[i] = GetPlayerState(Player(i), PLAYER_STATE_FOOD_CAP_CEILING);
		}
		if (cache) FoodCacheSaved = *cache;
		FoodApplied = true;
		if (cache) *cache = 65535;
		for (int i = 0; i < MAX_PLAYER_SLOTS; ++i) {
			SetPlayerState(Player(i), PLAYER_STATE_FOOD_CAP_CEILING, 65535);
		}
	} else {
		if (!FoodApplied) return;
		if (cache) *cache = FoodCacheSaved;
		for (int i = 0; i < MAX_PLAYER_SLOTS; ++i) {
			SetPlayerState(Player(i), PLAYER_STATE_FOOD_CAP_CEILING, FoodPlayerSaved[i]);
		}
		FoodApplied = false;
	}
}

// 娱乐模式只还原了代码（不在游戏线程 / 对局已经换了）之后，在游戏线程里修正人口上限：
// 如果这一局读图时补丁还在，游戏已经按补丁算出 65535 并复制给每个玩家。缓存为 65535 时
// 说明是补丁的结果（原版规则最多 300），重新按原版规则计算，并只修正仍是 65535 的玩家
// （地图触发器自己设置的人口上限不动）。
static void FoodCeilingRecompute(W3T_Shared* shm) {
	FoodRecomputePending = false;
	if (shm->toggleState[TGL_FUN_MODE]) return;         // 又开着娱乐模式：保持 65535
	int* cache = reinterpret_cast<int*>(Offset(GLOBAL_FOOD_CEILING_CACHE));
	void* fn = Offset(GAME_FOOD_CEILING_GET);
	if (!cache || !fn || *cache != 65535) return;
	*cache = -1;
	int value = aero::generic_c_call<int>(fn);
	for (int i = 0; i < MAX_PLAYER_SLOTS; ++i) {
		handle p = Player(i);
		if (GetPlayerState(p, PLAYER_STATE_FOOD_CAP_CEILING) == 65535) SetPlayerState(p, PLAYER_STATE_FOOD_CAP_CEILING, value);
	}
}

// 关闭全部常驻开关。inGame = false 时只还原代码字节（不能调用游戏函数，或者游戏对象已经不在）：
// 人口上限由下一次进入对局时的 FoodCeilingRecompute 修正
static void TogglesRestoreAll(W3T_Shared* shm, bool inGame) {
	if (inGame) SafeRun([&] { FoodCeilingApply(false); });
	else if (FoodApplied || (shm && shm->toggleState[TGL_FUN_MODE])) FoodRecomputePending = true;
	FoodApplied = false;
	PatchRestoreAll();
	if (shm) {
		for (int id = 0; id < W3T_TOGGLE_SLOTS; ++id) {
			shm->toggleState[id] = 0;
			shm->toggleWant[id] = 0;
		}
	}
}

// ---------------------------------------------------------------------------
// 命令属性
// ---------------------------------------------------------------------------
enum {
	NEED_SELECTION = 0x1,   // 需要选中单位
	NEED_MOUSE = 0x2,       // 需要鼠标地图坐标
	NEED_HERO = 0x4         // 选中单位必须是英雄
};

static int CommandFlags(int cmd) {
	switch (cmd) {
	case CMD_HERO_LEVELUP: return NEED_SELECTION | NEED_HERO;
	case CMD_TELEPORT: return NEED_SELECTION | NEED_MOUSE;
	case CMD_CLONE_SELF: return NEED_SELECTION | NEED_MOUSE;
	case CMD_CLONE: return NEED_SELECTION | NEED_MOUSE;
	case CMD_CLONE_MANY: return NEED_SELECTION | NEED_MOUSE;
	case CMD_COPY_ITEMS: return NEED_SELECTION | NEED_MOUSE;
	case CMD_KILL: case CMD_INVULNERABLE: case CMD_VULNERABLE: case CMD_RESET_COOLDOWN:
	case CMD_DROP_ITEMS: case CMD_SET_CHARGES: case CMD_SET_SCALE: case CMD_FULL_CONTROL:
	case CMD_PAUSE_UNIT: case CMD_NO_COLLISION: case CMD_KILL_PLAYER_UNITS:
	case CMD_ADD_ABILITY: case CMD_REMOVE_ABILITY: case CMD_ALL_AURAS: case CMD_ALL_PASSIVES:
	case CMD_ALL_BUFFS: case CMD_POLYMORPH: case CMD_OVERLAP_ABILITY: case CMD_GIVE_ARTIFACTS:
		return NEED_SELECTION;
	case CMD_ADD_ATTRIBUTES: case CMD_SKILL_POINTS:
		return NEED_SELECTION | NEED_HERO;
	case CMD_CREATE_ITEM: case CMD_CREATE_ALL_ITEMS: case CMD_SUMMON:
		return NEED_MOUSE;
	default:
		return 0;
	}
}

// ---------------------------------------------------------------------------
// 各命令实现（在 SafeRun 内调用）
// ---------------------------------------------------------------------------
static void CommandRun(const W3T_CmdSlot& slot, CommandResult& r) {
	const int cmd = slot.cmd;
	const int flags = CommandFlags(cmd);
	handle unit = 0;
	float mx = 0.0f, my = 0.0f;
	wchar_t code[5];

	if (flags & NEED_SELECTION) {
		unit = SelectedUnitGet();
		if (!unit) { ResultSet(r, RES_NO_SELECTION, L"请先在游戏里框选一个单位"); return; }
		if ((flags & NEED_HERO) && !IsUnitType(unit, UNIT_TYPE_HERO)) {
			ResultSet(r, RES_FAILED, L"选中的单位不是英雄"); return;
		}
	}
	if (flags & NEED_MOUSE) {
		if (!MouseWorldPosGet(mx, my)) { ResultSet(r, RES_FAILED, L"读取鼠标地图坐标失败，请把鼠标移到地图上再按快捷键"); return; }
	}

	switch (cmd) {
	// ================= 单位 =================
	case CMD_HERO_LEVELUP: {   // [124E+0200]：取当前等级，循环 SetHeroLevel(单位, 等级+1, true)
		int levels = slot.iarg;
		if (levels < 1) levels = 1;
		if (levels > 100000) levels = 100000;
		int before = GetHeroLevel(unit);
		if (levels <= 20) {
			for (int i = 1; i <= levels; ++i) SetHeroLevel(unit, before + i, 1);
		} else {
			SetHeroLevel(unit, before + levels, 1);
		}
		int after = GetHeroLevel(unit);
		if (after > before) ResultSet(r, RES_OK, L"英雄等级 %d → %d", before, after);
		else ResultSet(r, RES_FAILED, L"英雄已到最高等级 %d（可打开“英雄最大等级”开关）", before);
		break;
	}
	case CMD_TELEPORT:         // [124E+0280]：SetUnitPosition(单位, 鼠标X, 鼠标Y)
		SetUnitPosition(unit, mx, my);
		ResultSet(r, RES_OK, L"已移动到 (%.0f, %.0f)", mx, my);
		break;
	case CMD_KILL:             // [124E+0300]
		KillUnit(unit);
		ResultSet(r, RES_OK, L"已杀死目标单位");
		break;
	case CMD_INVULNERABLE:     // [124E+0480]
		SetUnitInvulnerable(unit, 1);
		ResultSet(r, RES_OK, L"选中单位已无敌");
		break;
	case CMD_VULNERABLE:       // [124E+0500]
		SetUnitInvulnerable(unit, 0);
		ResultSet(r, RES_OK, L"已取消无敌");
		break;
	case CMD_RESET_COOLDOWN:   // [124E+0580]
		UnitResetCooldown(unit);
		ResultSet(r, RES_OK, L"技能CD已重置");
		break;
	case CMD_CLONE_SELF: {     // [124E+0600]：CreateUnit(本地玩家, 单位类型, 鼠标坐标)
		int typeId = GetUnitTypeId(unit);
		handle created = CreateUnit(GetLocalPlayer(), typeId, mx, my, GetUnitFacing(unit));
		CodeText(typeId, code);
		ResultSet(r, created ? RES_OK : RES_FAILED, created ? L"已复制 %ls 给自己" : L"复制 %ls 失败", code);
		break;
	}
	case CMD_CLONE: {          // [124E+0900]：CreateUnitAtLoc(原主人, 单位类型, 鼠标位置)
		int typeId = GetUnitTypeId(unit);
		handle created = CreateUnit(GetOwningPlayer(unit), typeId, mx, my, GetUnitFacing(unit));
		CodeText(typeId, code);
		ResultSet(r, created ? RES_OK : RES_FAILED, created ? L"已复制 %ls" : L"复制 %ls 失败", code);
		break;
	}
	case CMD_CLONE_MANY: {     // [124E+0B00]：复制 copycounter 个（给原主人，用于刷杀敌数）
		int count = slot.iarg;
		if (count < 1) count = 1;
		if (count > 500) count = 500;
		int typeId = GetUnitTypeId(unit);
		handle owner = GetOwningPlayer(unit);
		float face = GetUnitFacing(unit);
		int made = 0;
		for (int i = 0; i < count; ++i) if (CreateUnit(owner, typeId, mx, my, face)) ++made;
		CodeText(typeId, code);
		ResultSet(r, made ? RES_OK : RES_FAILED, L"已复制 %d 个 %ls", made, code);
		break;
	}
	case CMD_COPY_ITEMS: {     // [124E+0680]：背包 6 格逐格取物品类型，在鼠标位置 CreateItem
		int made = 0;
		for (int i = 0; i < 6; ++i) {
			handle item = UnitItemInSlot(unit, i);
			if (item && CreateItem(GetItemTypeId(item), mx, my)) ++made;
		}
		ResultSet(r, made ? RES_OK : RES_FAILED, made ? L"已复制 %d 件物品到鼠标位置" : L"背包里没有物品", made);
		break;
	}
	case CMD_DROP_ITEMS: {     // [124E+0800]：从第 6 格到第 1 格 UnitRemoveItemFromSlot
		int dropped = 0;
		for (int i = 5; i >= 0; --i) if (UnitRemoveItemFromSlot(unit, i)) ++dropped;
		ResultSet(r, RES_OK, L"已丢弃 %d 件物品", dropped);
		break;
	}
	case CMD_SET_CHARGES: {    // [124E+0780]：第 1 格物品 SetItemCharges(物品, 数量)
		handle item = UnitItemInSlot(unit, 0);
		if (!item) { ResultSet(r, RES_FAILED, L"背包第 1 格没有物品"); break; }
		SetItemCharges(item, slot.iarg);
		ResultSet(r, RES_OK, L"第 1 格物品数量设为 %d", slot.iarg);
		break;
	}
	case CMD_SET_SCALE: {      // [124E+0700]：SetUnitScale(单位, s, s, s)
		float s = slot.farg;
		if (!(s >= 0.05f && s <= 20.0f)) { ResultSet(r, RES_FAILED, L"大小需在 0.05 ~ 20 之间"); break; }
		SetUnitScale(unit, s, s, s);
		ResultSet(r, RES_OK, L"单位大小设为 %.2f", s);
		break;
	}
	case CMD_FULL_CONTROL: {   // [124E+0980]：SetPlayerAlliance(所属玩家, 本地玩家, 6/7, true)
		handle owner = GetOwningPlayer(unit);
		handle local = GetLocalPlayer();
		if (owner == local) { ResultSet(r, RES_FAILED, L"这已经是你自己的单位"); break; }
		SetPlayerAlliance(owner, local, ALLIANCE_SHARED_CONTROL, 1);
		SetPlayerAlliance(owner, local, ALLIANCE_SHARED_ADVANCED_CONTROL, 1);
		ResultSet(r, RES_OK, L"已获得玩家 %d 的完全控制权", GetPlayerId(owner) + 1);
		break;
	}
	case CMD_PAUSE_UNIT: {     // [127A] 暂停单位
		bool paused = IsUnitPaused(unit);
		PauseUnit(unit, paused ? 0 : 1);
		ResultSet(r, RES_OK, paused ? L"单位已恢复" : L"单位已暂停");
		break;
	}
	case CMD_NO_COLLISION: {   // [127A] 无视碰撞体积
		// 记录里的句柄必须仍是同一个单位（句柄会被回收），否则视为“未关闭”
		std::map<handle, UnitRef>::iterator it = NoCollisionUnits.find(unit);
		bool off = it != NoCollisionUnits.end() && UnitRefSameObject(it->second);
		SetUnitPathing(unit, off ? 1 : 0);
		if (off) NoCollisionUnits.erase(it); else NoCollisionUnits[unit] = UnitRefMake(unit);
		ResultSet(r, RES_OK, off ? L"已恢复碰撞体积" : L"已无视碰撞体积（再按一次恢复）");
		break;
	}
	case CMD_KILL_PLAYER_UNITS: {  // [127A] 秒杀玩家的所有单位
		handle owner = GetOwningPlayer(unit);
		if (owner == GetLocalPlayer()) { ResultSet(r, RES_FAILED, L"选中的是你自己的单位，已取消（请选中对方的单位）"); break; }
		handle group = CreateGroup();
		if (!group) { ResultSet(r, RES_FAILED, L"创建单位组失败"); break; }
		GroupEnumUnitsOfPlayer(group, owner, 0);
		// 先把单位全部取出再动手：边杀边取时，死亡触发器移除的单位会成为组里的“空位”，
		// FirstOfGroup 遇到它就返回 0，循环会提前结束
		std::vector<UnitRef> units;
		for (int guard = 0; guard < 8192; ++guard) {
			handle u = FirstOfGroup(group);
			if (!u) break;
			units.push_back(UnitRefMake(u));
			GroupRemoveUnit(group, u);
		}
		DestroyGroup(group);
		int killed = 0;
		for (size_t i = 0; i < units.size(); ++i) {
			if (!UnitRefValid(units[i])) continue;      // 已被前面的死亡触发器移除 / 句柄被回收
			KillUnit(units[i].unit);
			++killed;
		}
		ResultSet(r, RES_OK, L"已秒杀玩家 %d 的 %d 个单位", GetPlayerId(owner) + 1, killed);
		break;
	}

	// ================= 英雄 / 技能 =================
	case CMD_ADD_ABILITY: {    // [124E+0880]：内部函数添加 + SetUnitAbilityLevel(…, 0x70)
		int id = slot.iarg;
		CodeText(id, code);
		bool added = UnitAddAbility(unit, id);
		if (!added) added = AbilityAddInternal(unit, id);   // 原生函数拒绝（已有同名技能等）时，按原版用内部函数强制添加
		SetUnitAbilityLevel(unit, id, kMaxAbilityLevel);
		ResultSet(r, added ? RES_OK : RES_FAILED, added ? L"已添加技能 %ls" : L"添加技能 %ls 失败（代码无效？）", code);
		break;
	}
	case CMD_REMOVE_ABILITY: { // [124E+0E00]：UnitRemoveAbility
		int id = slot.iarg;
		CodeText(id, code);
		bool removed = UnitRemoveAbility(unit, id);
		ResultSet(r, removed ? RES_OK : RES_FAILED, removed ? L"已删除技能 %ls" : L"单位没有技能 %ls", code);
		break;
	}
	case CMD_ALL_AURAS: {      // [124E+0E20]
		for (size_t i = 0; i < sizeof(kAuraCodes) / sizeof(kAuraCodes[0]); ++i) {
			int id = FourCC(kAuraCodes[i]);
			UnitAddAbility(unit, id);
			SetUnitAbilityLevel(unit, id, kMaxAbilityLevel);
		}
		UnitAddAbility(unit, FourCC(kAuraNoLevel));
		ResultSet(r, RES_OK, L"已添加全部光环");
		break;
	}
	case CMD_ALL_PASSIVES: {   // [124E+0F50]
		UnitAddAbility(unit, FourCC("AInv"));
		for (size_t i = 0; i < sizeof(kPassiveCodes) / sizeof(kPassiveCodes[0]); ++i) {
			int id = FourCC(kPassiveCodes[i]);
			UnitAddAbility(unit, id);
			SetUnitAbilityLevel(unit, id, kMaxAbilityLevel);
		}
		ResultSet(r, RES_OK, L"已添加全部被动（重击 / 致命一击 / 醉拳）");
		break;
	}
	case CMD_ALL_BUFFS: {      // [120E+1280] 心灵之火 / 嗜血 / 邪恶狂热 / 咆哮
		// 原版直接构造 CAbility 对象调用虚函数，版本间虚表不同、1.24E 原版已删除该功能；
		// 这里改用隐形施法单位，所有调用都是 JASS 原生函数，全版本通用。
		static const char* const kInnerFire[] = { "ACif", "Ainf" };
		static const char* const kBloodlust[] = { "ACbl", "Ablo" };
		static const char* const kUnholy[] = { "ACuf", "Auhf" };
		static const char* const kRoar[] = { "ACro", "Aroa" };
		handle owner = GetOwningPlayer(unit);
		int ok = 0;
		ok += CastWithDummy(owner, unit, kInnerFire, 2, ORDER_INNERFIRE, false) ? 1 : 0;
		ok += CastWithDummy(owner, unit, kBloodlust, 2, ORDER_BLOODLUST, false) ? 1 : 0;
		ok += CastWithDummy(owner, unit, kUnholy, 2, ORDER_UNHOLYFRENZY, false) ? 1 : 0;
		ok += CastWithDummy(owner, unit, kRoar, 2, ORDER_ROAR, true) ? 1 : 0;
		ResultSet(r, ok ? RES_OK : RES_FAILED, L"已施放 %d/4 个增益（心灵之火 / 嗜血 / 邪恶狂热 / 咆哮）", ok);
		break;
	}
	case CMD_POLYMORPH: {      // [120E+1300] 变绵羊
		static const char* const kPoly[] = { "ACpy", "Aply" };
		handle owner = GetOwningPlayer(unit);
		// 施法单位必须是目标的敌人：默认用中立敌对玩家，目标本身是中立敌对时用本地玩家
		handle caster = Player(PLAYER_NEUTRAL_AGGRESSIVE);
		if (owner == caster) caster = GetLocalPlayer();
		bool ok = CastWithDummy(caster, unit, kPoly, 2, ORDER_POLYMORPH, false);
		ResultSet(r, ok ? RES_OK : RES_FAILED, ok ? L"咩~ 已变成绵羊" : L"变羊失败（英雄 / 魔免单位不能被变形）");
		break;
	}
	case CMD_ADD_ATTRIBUTES: { // [127A] 设置属性
		int n = slot.iarg;
		SetHeroStr(unit, GetHeroStr(unit, 0) + n, 1);
		SetHeroAgi(unit, GetHeroAgi(unit, 0) + n, 1);
		SetHeroInt(unit, GetHeroInt(unit, 0) + n, 1);
		ResultSet(r, RES_OK, L"力量 / 敏捷 / 智力各增加 %d", n);
		break;
	}
	case CMD_SKILL_POINTS: {   // [127A] 增加技能点数
		bool ok = UnitModifySkillPoints(unit, slot.iarg);
		ResultSet(r, ok ? RES_OK : RES_FAILED, ok ? L"技能点数增加 %d" : L"增加技能点数失败", slot.iarg);
		break;
	}
	case CMD_OVERLAP_ABILITY: { // [120E+1200]：内部函数重复添加 10 次 + SetUnitAbilityLevel(…, 0x70)
		int id = slot.iarg;
		CodeText(id, code);
		if (!Offset(UNIT_ADD_ABILITY_INTERNAL)) { ResultSet(r, RES_UNSUPPORTED, L"当前版本没有内部添加技能函数，无法重叠技能"); break; }
		int added = 0;
		for (int i = 0; i < 10; ++i) {
			if (AbilityAddInternal(unit, id)) ++added;
			SetUnitAbilityLevel(unit, id, kMaxAbilityLevel);
		}
		ResultSet(r, added ? RES_OK : RES_FAILED, L"技能 %ls 已重叠添加 %d 次", code, added);
		break;
	}

	// ================= 物品 / 资源 / 科技 =================
	case CMD_GIVE_ARTIFACTS: { // [124E+0FE0]：先加物品栏，4 把瑟拉思尔 + 火焰手套 + 远古战斧
		UnitAddAbility(unit, FourCC("AInv"));
		int got = 0;
		for (int i = 5; i >= 2; --i) got += UnitAddItemToSlotById(unit, FourCC("srtl"), i) ? 1 : 0;
		got += UnitAddItemToSlotById(unit, FourCC("frhg"), 1) ? 1 : 0;
		got += UnitAddItemToSlotById(unit, FourCC("klmm"), 0) ? 1 : 0;
		ResultSet(r, RES_OK, L"已得到 %d 件神器（背包已满的格子会掉在地上）", got);
		break;
	}
	case CMD_CREATE_ITEM: {    // [124E+1100]：CreateItem(物品代码, 鼠标坐标)
		CodeText(slot.iarg, code);
		handle item = CreateItem(slot.iarg, mx, my);
		ResultSet(r, item ? RES_OK : RES_FAILED, item ? L"已创建物品 %ls" : L"创建物品 %ls 失败（代码无效？）", code);
		break;
	}
	case CMD_CREATE_ALL_ITEMS: {   // [120E+1180]：遍历物品数据表，逐个 CreateItem
		war3::ItemDataHashTable* table = reinterpret_cast<war3::ItemDataHashTable*>(Offset(GLOBAL_ITEMDATA_TABLE));
		if (!table) { ResultSet(r, RES_UNSUPPORTED, L"当前版本没有物品数据表地址"); break; }
		int made = 0;
		war3::ItemDataNode* node = table->firstNode;
		for (int guard = 0; guard < 4096 && (intptr_t)node > 0; ++guard) {
			if (node->typeId && CreateItem((int)node->typeId, mx, my)) ++made;
			node = *reinterpret_cast<war3::ItemDataNode**>(reinterpret_cast<uint8_t*>(node) + table->linkOffset + 4);
		}
		ResultSet(r, made ? RES_OK : RES_FAILED, L"已创建 %d 件物品", made);
		break;
	}
	case CMD_MONEY_SELF: {     // [127A] 增加金币木材（只给自己）
		handle local = GetLocalPlayer();
		SetPlayerState(local, PLAYER_STATE_RESOURCE_GOLD, slot.iarg);
		SetPlayerState(local, PLAYER_STATE_RESOURCE_LUMBER, slot.iarg);
		ResultSet(r, RES_OK, L"金钱和木材设为 %d", slot.iarg);
		break;
	}
	case CMD_MONEY_ALL: {      // [124E+0A80]：玩家 11 到 0 全部设为指定数量
		for (int i = MAX_PLAYER_SLOTS - 1; i >= 0; --i) {
			handle p = Player(i);
			SetPlayerState(p, PLAYER_STATE_RESOURCE_GOLD, slot.iarg);
			SetPlayerState(p, PLAYER_STATE_RESOURCE_LUMBER, slot.iarg);
		}
		ResultSet(r, RES_OK, L"所有玩家的金钱和木材设为 %d", slot.iarg);
		break;
	}
	case CMD_SUMMON: {         // [124E+1080]：CreateUnitAtLoc(本地玩家, 增援代码, 鼠标位置)
		CodeText(slot.iarg, code);
		handle created = CreateUnit(GetLocalPlayer(), slot.iarg, mx, my, 270.0f);
		ResultSet(r, created ? RES_OK : RES_FAILED, created ? L"增援 %ls 已到达" : L"呼叫增援 %ls 失败（代码无效？）", code);
		break;
	}
	case CMD_RESEARCH: {       // [124E+1180]：SetPlayerTechResearched(本地玩家, 科技代码, 0x70)
		CodeText(slot.iarg, code);
		SetPlayerTechResearched(GetLocalPlayer(), slot.iarg, kMaxAbilityLevel);
		ResultSet(r, RES_OK, L"已得到科技 %ls", code);
		break;
	}
	case CMD_XP_RATE: {        // [127A] 增加经验获取率
		float rate = slot.farg;
		if (!(rate >= 0.0f && rate <= 1000.0f)) { ResultSet(r, RES_FAILED, L"倍率需在 0 ~ 1000 之间"); break; }
		SetPlayerHandicapXP(GetLocalPlayer(), rate);
		ResultSet(r, RES_OK, L"经验获取率设为 %.0f%%", rate * 100.0f);
		break;
	}

	// ================= 游戏 =================
	case CMD_FOG_OFF:          // [124E+0380] MapON
		FogEnable(0);
		FogMaskEnable(0);
		ResultSet(r, RES_OK, L"已关闭战争迷雾");
		break;
	case CMD_FOG_ON:           // [124E+0400] MapOFF
		FogEnable(1);
		FogMaskEnable(1);
		ResultSet(r, RES_OK, L"已恢复战争迷雾");
		break;
	case CMD_PAUSE_GAME:       // [127A] 暂停游戏 / 恢复游戏
		GamePausedByTrainer = !GamePausedByTrainer;
		PauseGame(GamePausedByTrainer ? 1 : 0);
		ResultSet(r, RES_OK, GamePausedByTrainer ? L"游戏已暂停（再按一次继续）" : L"游戏已继续");
		break;

	default:
		ResultSet(r, RES_FAILED, L"未知命令 %d", cmd);
		break;
	}
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
// gameThread = true 时（每帧挂钩里）才在游戏画面上显示文字：游戏函数只能在游戏线程调用
static void ResultPublish(W3T_Shared* shm, LONG seq, int cmd, const CommandResult& r, bool gameThread) {
	if (!shm) return;
	// 结果环：先把条目的 counter 清 0，写内容，最后写 counter（界面按顺序锁方式读取，不会读到写了一半的条目）
	LONG n = shm->resultCounter + 1;
	W3T_ResultEntry& e = shm->results[(DWORD)n % W3T_RESULT_RING];
	InterlockedExchange(&e.counter, 0);
	e.seq = seq;
	e.cmd = cmd;
	e.code = r.code;
	memcpy(e.text, r.text, sizeof(e.text));
	InterlockedExchange(&e.counter, n);
	// 最近一条的副本
	shm->resultCmd = cmd;
	shm->resultCode = r.code;
	memcpy(shm->resultText, r.text, sizeof(shm->resultText));
	InterlockedExchange(&shm->resultSeq, seq);
	InterlockedExchange(&shm->resultCounter, n);

	if (gameThread && shm->inGameMessages && r.code != RES_NOT_IN_GAME) {
		// 魔兽内部文字是 UTF-8；先拼宽字符串再统一转换，不依赖编译器的窄字符串编码
		wchar_t wide[200];
		_snwprintf(wide, 199, L"|cffffcc00[修改器]|r %ls", r.text);
		wide[199] = 0;
		char utf8[600];
		WideToUtf8(wide, utf8, sizeof(utf8));
		SafeRun([&] { if (IsInGame()) TextPrint(4.0f, utf8); });
	}
}

static void NoticePublish(W3T_Shared* shm, int code, const wchar_t* text, bool gameThread) {
	CommandResult r;
	ResultSet(r, code, L"%ls", text);
	ResultPublish(shm, shm->resultSeq, 0, r, gameThread);
}

static bool AnyToggleOn(W3T_Shared* shm) {
	for (int id = 0; id < W3T_TOGGLE_SLOTS; ++id) if (shm->toggleState[id]) return true;
	return false;
}

// 换图 / 退出游戏：清空本局的记录（句柄已经属于上一局）
static void GameStateReset() {
	DummyUnits.clear();
	NoCollisionUnits.clear();
	GamePausedByTrainer = false;
	MultiplayerWarned = false;
}

// 对局切换检查（两个线程共用，只读内存、只还原代码字节）：
// 退出游戏或换了一局时，常驻开关一律关闭，不让补丁带进下一局（读图时游戏会缓存部分数值，
// 下一局还可能是多人游戏）。gameObject = NULL 表示不在对局中。
static void GameTransitionCheck(W3T_Shared* shm, void* gameObject, bool gameThread) {
	if (gameObject != LastGameObject) {
		if (LastGameObject && AnyToggleOn(shm)) {
			TogglesRestoreAll(shm, false);
			NoticePublish(shm, RES_OK, L"已退出游戏，常驻开关已全部关闭", gameThread);
		}
		GameStateReset();
		LastGameObject = gameObject;
	}
	if (!gameObject && kTogglesNeedGame && AnyToggleOn(shm)) {     // 兜底：不在对局中不允许有开着的开关
		TogglesRestoreAll(shm, false);
		NoticePublish(shm, RES_OK, L"已退出游戏，常驻开关已全部关闭", gameThread);
	}
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
void Trainer_Execute(W3T_Shared* shm, const W3T_CmdSlot& slot) {
	CommandResult r;
	ResultSet(r, RES_OK, L"");
	bool ran = SafeRun([&] {
		if (!jass::NativesComplete(NULL)) { ResultSet(r, RES_UNSUPPORTED, L"当前游戏版本的函数表不完整，无法执行"); return; }
		if (!IsInGame()) { ResultSet(r, RES_NOT_IN_GAME, L"请先进入地图（主菜单 / 读图时不能使用）"); return; }
		if (HumanPlayersCount() > 1) { ResultSet(r, RES_MULTIPLAYER, L"检测到多人游戏，修改器只能在单人游戏中使用"); return; }
		CommandRun(slot, r);
	});
	if (!ran) {
		ResultSet(r, RES_EXCEPTION, L"执行时发生异常 %08lX @ %p，已拦截（游戏未受影响）",
			SafeLastExceptionCode(), SafeLastExceptionAddress());
	}
	ResultPublish(shm, slot.seq, slot.cmd, r, true);
}

void Trainer_Reject(W3T_Shared* shm, const W3T_CmdSlot& slot, bool inGame) {
	CommandResult r;
	if (inGame) ResultSet(r, RES_FAILED, L"游戏画面没有在刷新（窗口最小化？），请切回游戏后再试");
	else ResultSet(r, RES_NOT_IN_GAME, L"请先进入地图（主菜单 / 读图时不能使用）");
	ResultPublish(shm, slot.seq, slot.cmd, r, false);
}

void Trainer_SyncToggles(W3T_Shared* shm, bool gameThread) {
	static const wchar_t* const kNames[TGL_COUNT] = { L"不会失败", L"娱乐模式", L"允许光环叠加", L"英雄最大等级", L"选中单位无CD无蓝耗" };
	// 后台线程：只读内存判断是否在对局中（不调用游戏函数）
	bool idleInGame = false;
	if (!gameThread) SafeRun([&] { idleInGame = jass::NativesComplete(NULL) && IsInGame(); });

	for (int id = 0; id < TGL_COUNT; ++id) {
		bool want = shm->toggleWant[id] != 0;
		bool have = shm->toggleState[id] != 0;
		int argument = shm->toggleArg[id];
		if (want == have && !(want && id == TGL_MAX_HERO_LEVEL)) continue;

		CommandResult r;
		ResultSet(r, RES_OK, L"");
		bool ok = false;
		bool ran = SafeRun([&] {
			if (!gameThread && idleInGame) {
				// 对局还在，只是画面没有刷新（窗口最小化）：要等回到游戏线程才能处理
				ResultSet(r, RES_FAILED, L"%ls：游戏画面没有在刷新（窗口最小化？），请切回游戏后再操作", kNames[id]);
				return;
			}
			// 开启只能在单人游戏进行中：主菜单 / 大厅里打开的补丁会带进下一局（可能是多人游戏），
			// 而且读图时游戏会缓存部分数值（例如人口上限），之后再还原代码也撤销不了。关闭任何时候都可以。
			if (want && !have && kTogglesNeedGame) {
				if (!gameThread || !jass::NativesComplete(NULL) || !IsInGame()) {
					ResultSet(r, RES_NOT_IN_GAME, L"%ls：请先进入单人游戏再开启（退出游戏时会自动关闭）", kNames[id]);
					return;
				}
				if (HumanPlayersCount() > 1) {
					ResultSet(r, RES_MULTIPLAYER, L"检测到多人游戏，常驻开关只能在单人游戏中使用");
					return;
				}
			}
			if (id == TGL_NOCD_NOMANA) {        // 不是代码补丁，由每帧刷新维持
				ok = true;
				return;
			}
			bool inGame = gameThread && IsInGame();
			if (id == TGL_FUN_MODE && !want && inGame) FoodCeilingApply(false);     // 先还原人口，再还原代码
			wchar_t reason[160];
			reason[0] = 0;
			ok = PatchApply(id, want, argument, reason, 160);
			if (!ok) {
				ResultSet(r, PatchSupported(id) ? RES_FAILED : RES_UNSUPPORTED, L"%ls", reason[0] ? reason : L"当前版本不支持该开关");
				return;
			}
			if (id == TGL_FUN_MODE) {
				if (want && inGame) FoodCeilingApply(true);
				else if (!want && !inGame) {            // 只还原了代码：下一次进入对局时修正人口上限
					FoodApplied = false;
					FoodRecomputePending = true;
				}
			}
		});
		if (!ran) {
			ResultSet(r, RES_EXCEPTION, L"切换开关时发生异常 %08lX，已拦截", SafeLastExceptionCode());
		}
		if (ok) {
			shm->toggleState[id] = want ? 1 : 0;
			if (id == TGL_MAX_HERO_LEVEL && want && have) continue;   // 只是更新数值，不提示
		} else {
			shm->toggleWant[id] = shm->toggleState[id];             // 失败：界面恢复原状态
		}
		if (ok) ResultSet(r, RES_OK, L"%ls：%ls", kNames[id], want ? L"已开启" : L"已关闭");
		ResultPublish(shm, shm->resultSeq, -(id + 1), r, gameThread);
	}
}

void Trainer_Tick(W3T_Shared* shm) {
	DWORD now = GetTickCount();

	SafeRun([&] {
		bool complete = jass::NativesComplete(NULL);
		bool inGame = complete && IsInGame();
		void* gameObject = inGame ? GameObjectGet() : NULL;
		shm->inGame = inGame ? 1 : 0;
		GameTransitionCheck(shm, gameObject, true);
		if (!inGame) {
			shm->selValid = 0;
			return;
		}
		if (FoodRecomputePending) FoodCeilingRecompute(shm);

		// 施法完成（当前命令回到 0）的施法单位立即删除；最长保留 30 秒。
		// 用命令状态而不是固定时间判断：游戏暂停时施法也会暂停，不会被提前删掉。
		for (size_t i = 0; i < DummyUnits.size();) {
			PendingRemoval& pr = DummyUnits[i];
			bool valid = UnitRefValid(pr.ref);
			bool done = !valid || (now - pr.created > 300 && GetUnitCurrentOrder(pr.ref.unit) == 0) ||
				(LONG)(now - pr.deadline) >= 0;
			if (done) {
				if (valid) RemoveUnit(pr.ref.unit);
				DummyUnits.erase(DummyUnits.begin() + i);
			} else {
				++i;
			}
		}

		// 每 250ms 刷新一次真人玩家数和选中单位
		if (now - LastSelectionRefresh < 250) return;
		LastSelectionRefresh = now;

		int humans = HumanPlayersCount();
		shm->humanPlayers = humans;
		shm->localPlayerId = GetPlayerId(GetLocalPlayer());

		// 多人游戏：自动关闭所有常驻开关（正常情况下开关只能在单人游戏中打开，这里是兜底）
		if (humans > 1) {
			bool any = AnyToggleOn(shm);
			if (any) TogglesRestoreAll(shm, true);
			for (int id = 0; id < W3T_TOGGLE_SLOTS; ++id) shm->toggleWant[id] = 0;
			if (any || !MultiplayerWarned) {
				NoticePublish(shm, RES_MULTIPLAYER, any ? L"检测到多人游戏，已自动关闭所有常驻开关" : L"检测到多人游戏，修改器功能已停用", true);
				MultiplayerWarned = true;
			}
			shm->selValid = 0;
			return;
		}

		handle unit = SelectedUnitGet();
		if (unit) {
			shm->selTypeId = (DWORD)GetUnitTypeId(unit);
			shm->selOwnerId = GetPlayerId(GetOwningPlayer(unit));
			shm->selHeroLevel = IsUnitType(unit, UNIT_TYPE_HERO) ? GetHeroLevel(unit) : 0;
			shm->selValid = 1;
		} else {
			shm->selValid = 0;
		}

		// 选中单位无CD无蓝耗 [127A 无CD无蓝耗]
		if (shm->toggleState[TGL_NOCD_NOMANA] && unit) {
			UnitResetCooldown(unit);
			SetUnitState(unit, UNIT_STATE_MANA, GetUnitState(unit, UNIT_STATE_MAX_MANA));
		}
	});
}

bool Trainer_Idle(W3T_Shared* shm) {
	bool inGame = false;
	void* gameObject = NULL;
	// 只读内存：游戏对象可能正在释放，读错由 SafeRun 拦截
	SafeRun([&] {
		inGame = jass::NativesComplete(NULL) && IsInGame();
		gameObject = inGame ? GameObjectGet() : NULL;
	});
	shm->inGame = inGame ? 1 : 0;
	if (!inGame) shm->selValid = 0;
	GameTransitionCheck(shm, gameObject, false);
	return inGame;
}

void Trainer_Shutdown(W3T_Shared* shm, bool gameThread) {
	bool inGame = false;
	if (gameThread) {
		SafeRun([&] {
			inGame = jass::NativesComplete(NULL) && IsInGame();
			if (inGame) {
				for (size_t i = 0; i < DummyUnits.size(); ++i) {
					if (UnitRefValid(DummyUnits[i].ref)) RemoveUnit(DummyUnits[i].ref.unit);
				}
				if (GamePausedByTrainer) PauseGame(0);
			}
		});
	}
	GameStateReset();
	TogglesRestoreAll(shm, inGame);
}
