// 模块说明：界面功能表，见 Rows.h
#include "stdafx.h"
#include "Rows.h"

const wchar_t* const kGroupNames[GROUP_COUNT] = {
	L"单位（先在游戏里框选一个单位，复制 / 移动类功能以鼠标位置为准）",
	L"英雄 / 技能",
	L"重叠技能（同一技能重复添加 10 次；光环类不会叠加效果）",
	L"物品 / 资源 / 科技",
	L"游戏",
	L"常驻开关（按一次开启，再按一次关闭）",
};

#define CMD(id, group, key, name, hotkey, arg, value, need, tip) { ROW_COMMAND, id, group, key, name, hotkey, arg, value, need, tip }
#define TGL(id, key, name, hotkey, arg, value, need, tip) { ROW_TOGGLE, id, GROUP_TOGGLE, key, name, hotkey, arg, value, need, tip }

const RowDef kRows[] = {
	// ---------------- 单位 ----------------
	CMD(CMD_HERO_LEVELUP,      GROUP_UNIT, L"HeroLevelUp",     L"英雄升级（一次升 N 级）",          L"Ctrl+Q",       ARG_INT,   L"10",      0, L"原版 1.24E Ctrl+Q：SetHeroLevel 逐级提升"),
	CMD(CMD_TELEPORT,          GROUP_UNIT, L"Teleport",        L"瞬间移动（到鼠标位置）",            L"Ctrl+X",       ARG_NONE,  L"",        0, L"原版 Ctrl+X：SetUnitPosition"),
	CMD(CMD_KILL,              GROUP_UNIT, L"KillUnit",        L"杀掉目标单位",                    L"Ctrl+W",       ARG_NONE,  L"",        0, L"原版 Ctrl+W：KillUnit"),
	CMD(CMD_INVULNERABLE,      GROUP_UNIT, L"Invulnerable",    L"无敌",                            L"Ctrl+E",       ARG_NONE,  L"",        0, L"原版 Ctrl+E：SetUnitInvulnerable(true)"),
	CMD(CMD_VULNERABLE,        GROUP_UNIT, L"Vulnerable",      L"取消无敌",                        L"Ctrl+R",       ARG_NONE,  L"",        0, L"原版 Ctrl+R：SetUnitInvulnerable(false)"),
	CMD(CMD_RESET_COOLDOWN,    GROUP_UNIT, L"ResetCooldown",   L"重置技能CD",                      L"Ctrl+Z",       ARG_NONE,  L"",        0, L"原版 Ctrl+Z：UnitResetCooldown"),
	CMD(CMD_CLONE_SELF,        GROUP_UNIT, L"CloneSelf",       L"复制给自己（鼠标位置）",            L"Ctrl+A",       ARG_NONE,  L"",        0, L"原版 Ctrl+A：复制选中单位给自己"),
	CMD(CMD_CLONE,             GROUP_UNIT, L"Clone",           L"复制单位（鼠标位置）",              L"Ctrl+B",       ARG_NONE,  L"",        0, L"原版 Ctrl+B：复制给单位原来的主人"),
	CMD(CMD_CLONE_MANY,        GROUP_UNIT, L"CloneMany",       L"大量复制（刷杀敌数）",              L"Ctrl+N",       ARG_INT,   L"10",      0, L"原版 Ctrl+N：一次复制 N 个（最多 500）"),
	CMD(CMD_COPY_ITEMS,        GROUP_UNIT, L"CopyItems",       L"复制背包物品（鼠标位置）",          L"Ctrl+D",       ARG_NONE,  L"",        0, L"原版 Ctrl+D：背包 6 格物品各复制一份"),
	CMD(CMD_DROP_ITEMS,        GROUP_UNIT, L"DropItems",       L"丢弃所有物品",                    L"Ctrl+T",       ARG_NONE,  L"",        0, L"原版 Ctrl+T"),
	CMD(CMD_SET_CHARGES,       GROUP_UNIT, L"SetCharges",      L"物品数量（背包第 1 格）",           L"Ctrl+F",       ARG_INT,   L"999",     0, L"原版 Ctrl+F：SetItemCharges"),
	CMD(CMD_SET_SCALE,         GROUP_UNIT, L"SetScale",        L"设置大小",                        L"Ctrl+P",       ARG_FLOAT, L"0.5",     0, L"原版 Ctrl+P：SetUnitScale"),
	CMD(CMD_FULL_CONTROL,      GROUP_UNIT, L"FullControl",     L"获得对方控制权",                  L"Ctrl+I",       ARG_NONE,  L"",        0, L"原版 Ctrl+I：选中单位所属玩家对你共享完全控制权"),
	CMD(CMD_PAUSE_UNIT,        GROUP_UNIT, L"PauseUnit",       L"暂停 / 恢复单位",                  L"Ctrl+U",       ARG_NONE,  L"",        0, L"1.27A：暂停单位"),
	CMD(CMD_NO_COLLISION,      GROUP_UNIT, L"NoCollision",     L"无视碰撞体积（开 / 关）",           L"Ctrl+V",       ARG_NONE,  L"",        0, L"1.27A：无视碰撞体积，再按一次恢复"),
	CMD(CMD_KILL_PLAYER_UNITS, GROUP_UNIT, L"KillPlayerUnits", L"秒杀该玩家的所有单位",            L"Ctrl+Shift+K", ARG_NONE,  L"",        0, L"1.27A：选中对方任意一个单位，杀死该玩家全部单位"),

	// ---------------- 英雄 / 技能 ----------------
	CMD(CMD_ADD_ABILITY,       GROUP_HERO, L"AddAbility",      L"添加技能（技能代码）",              L"Ctrl+G",       ARG_CODE,  L"AInv",    0, L"原版 Ctrl+G：添加后设为最高等级"),
	CMD(CMD_REMOVE_ABILITY,    GROUP_HERO, L"RemoveAbility",   L"删除技能（技能代码）",              L"Ctrl+J",       ARG_CODE,  L"AInv",    0, L"原版 Ctrl+J：UnitRemoveAbility"),
	CMD(CMD_ALL_AURAS,         GROUP_HERO, L"AllAuras",        L"全光环",                          L"Ctrl+Num1",    ARG_NONE,  L"",        0, L"原版 Ctrl+numeric 1：辉煌 / 专注 / 耐久 / 邪恶 / 吸血 / 强击 / 命令 / 治疗守卫"),
	CMD(CMD_ALL_PASSIVES,      GROUP_HERO, L"AllPassives",     L"全被动",                          L"Ctrl+Num2",    ARG_NONE,  L"",        0, L"原版 Ctrl+numeric 2：物品栏 / 重击 / 致命一击 / 醉拳"),
	CMD(CMD_ALL_BUFFS,         GROUP_HERO, L"AllBuffs",        L"全BUFF（心灵之火/嗜血/邪恶狂热/咆哮）", L"Alt+E",      ARG_NONE,  L"",        0, L"1.20E Alt+E：用隐形施法单位施放增益"),
	CMD(CMD_POLYMORPH,         GROUP_HERO, L"Polymorph",       L"最喜欢小动物了（变绵羊）",          L"Alt+D",        ARG_NONE,  L"",        0, L"1.20E Alt+D：把选中单位变成绵羊"),
	CMD(CMD_ADD_ATTRIBUTES,    GROUP_HERO, L"AddAttributes",   L"增加属性（力 / 敏 / 智各 +N）",     L"Ctrl+H",       ARG_INT,   L"100",     0, L"1.27A：设置属性"),
	CMD(CMD_SKILL_POINTS,      GROUP_HERO, L"SkillPoints",     L"增加技能点数",                    L"Ctrl+K",       ARG_INT,   L"10",      0, L"1.27A：增加技能点数"),

	// ---------------- 重叠技能（1.20E Down Arrow+numeric）----------------
	CMD(CMD_OVERLAP_ABILITY,   GROUP_OVERLAP, L"Overlap1",     L"重叠技能1（魔法恢复光环）",         L"Down+Num1",    ARG_CODE,  L"ANre",    W3T_SUPPORT_ABILINT, L"1.20E Down Arrow+numeric 1"),
	CMD(CMD_OVERLAP_ABILITY,   GROUP_OVERLAP, L"Overlap2",     L"重叠技能2（闪电链）",               L"Down+Num2",    ARG_CODE,  L"AOcl",    W3T_SUPPORT_ABILINT, L"1.20E Down Arrow+numeric 2"),
	CMD(CMD_OVERLAP_ABILITY,   GROUP_OVERLAP, L"Overlap3",     L"重叠技能3（震荡波）",               L"Down+Num3",    ARG_CODE,  L"AOs2",    W3T_SUPPORT_ABILINT, L"1.20E Down Arrow+numeric 3"),
	CMD(CMD_OVERLAP_ABILITY,   GROUP_OVERLAP, L"Overlap4",     L"重叠技能4（腐臭蜂群）",             L"Down+Num4",    ARG_CODE,  L"AUcs",    W3T_SUPPORT_ABILINT, L"1.20E Down Arrow+numeric 4"),
	CMD(CMD_OVERLAP_ABILITY,   GROUP_OVERLAP, L"Overlap5",     L"重叠技能5（尖刺外壳）",             L"Down+Num8",    ARG_CODE,  L"AUts",    W3T_SUPPORT_ABILINT, L"1.20E Down Arrow+numeric 8"),
	CMD(CMD_OVERLAP_ABILITY,   GROUP_OVERLAP, L"Overlap6",     L"重叠技能6（凤凰火焰）",             L"Down+Num9",    ARG_CODE,  L"Apxf",    W3T_SUPPORT_ABILINT, L"1.20E Down Arrow+numeric 9"),

	// ---------------- 物品 / 资源 / 科技 ----------------
	CMD(CMD_GIVE_ARTIFACTS,    GROUP_ITEM, L"GiveArtifacts",   L"得到6个神器（给选中单位）",         L"Ctrl+Y",       ARG_NONE,  L"",        0, L"原版 Ctrl+Y：4 把瑟拉思尔 + 火焰手套 + 远古战斧"),
	CMD(CMD_CREATE_ITEM,       GROUP_ITEM, L"CreateItem",      L"得到物品（请大象赐予装备）",        L"Ctrl+Num5",    ARG_CODE,  L"amrc",    0, L"原版 Ctrl+numeric 5：在鼠标位置创建物品"),
	CMD(CMD_CREATE_ALL_ITEMS,  GROUP_ITEM, L"CreateAllItems",  L"创建所有物品（请慎重使用）",        L"Ctrl+Num8",    ARG_NONE,  L"",        W3T_SUPPORT_ITEMLIST, L"1.20E Ctrl+numeric 8：在鼠标位置创建全部物品"),
	CMD(CMD_MONEY_ALL,         GROUP_ITEM, L"MoneyAll",        L"设置钱和木（所有玩家）",            L"Ctrl+L",       ARG_INT,   L"1000000", 0, L"原版 Ctrl+L：所有玩家 100 万金钱和木材"),
	CMD(CMD_MONEY_SELF,        GROUP_ITEM, L"MoneySelf",       L"设置钱和木（自己）",                L"Ctrl+M",       ARG_INT,   L"1000000", 0, L"1.27A：增加金币木材"),
	CMD(CMD_SUMMON,            GROUP_ITEM, L"Summon",          L"呼叫增援（单位代码）",              L"Ctrl+Num4",    ARG_CODE,  L"nbel",    0, L"原版 Ctrl+numeric 4：在鼠标位置创建单位给自己"),
	CMD(CMD_RESEARCH,          GROUP_ITEM, L"Research",        L"得到科技（科技代码）",              L"Ctrl+Num6",    ARG_CODE,  L"Rhde",    0, L"原版 Ctrl+numeric 6：SetPlayerTechResearched"),
	CMD(CMD_XP_RATE,           GROUP_ITEM, L"XpRate",          L"经验获取率（倍数）",                L"Ctrl+Num0",    ARG_FLOAT, L"10",      0, L"1.27A：增加经验获取率，1 = 正常"),

	// ---------------- 游戏 ----------------
	CMD(CMD_FOG_OFF,           GROUP_GAME, L"FogOff",          L"关闭战争迷雾（MapON）",             L"Home",         ARG_NONE,  L"",        0, L"原版 Home：FogEnable(false) / FogMaskEnable(false)"),
	CMD(CMD_FOG_ON,            GROUP_GAME, L"FogOn",           L"恢复战争迷雾（MapOFF）",            L"End",          ARG_NONE,  L"",        0, L"原版 End"),
	CMD(CMD_PAUSE_GAME,        GROUP_GAME, L"PauseGame",       L"暂停 / 继续游戏",                  L"Pause",        ARG_NONE,  L"",        0, L"1.27A：暂停游戏 / 恢复游戏"),

	// ---------------- 常驻开关 ----------------
	TGL(TGL_NO_DEFEAT,         L"NoDefeat",        L"不会失败",                        L"Ctrl+Num3",    ARG_NONE,  L"",        W3T_SUPPORT_PATCHES, L"原版 Ctrl+numeric 3：IsNoDefeatCheat 恒为真"),
	TGL(TGL_FUN_MODE,          L"FunMode",         L"娱乐模式（快速建造/建筑可重叠/人口65535）", L"Ctrl+Num7", ARG_NONE, L"",    W3T_SUPPORT_PATCHES, L"原版 Ctrl+numeric 7"),
	TGL(TGL_AURA_STACK,        L"AuraStack",       L"允许光环叠加",                    L"Ctrl+Num9",    ARG_NONE,  L"",        W3T_SUPPORT_PATCHES, L"1.20E Ctrl+numeric 9"),
	TGL(TGL_MAX_HERO_LEVEL,    L"MaxHeroLevel",    L"英雄最大等级",                    L"Alt+Num1",     ARG_INT,   L"100000",  W3T_SUPPORT_MAXLEVEL, L"1.20E Alt+numeric 1：英雄最大等级10W"),
	TGL(TGL_NOCD_NOMANA,       L"NoCdNoMana",      L"选中单位无CD无蓝耗",              L"Alt+Num2",     ARG_NONE,  L"",        0, L"1.27A：无CD无蓝耗（对当前选中的单位持续生效）"),
};

const int kRowCount = sizeof(kRows) / sizeof(kRows[0]);
