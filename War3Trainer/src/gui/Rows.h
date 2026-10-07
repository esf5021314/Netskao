// 模块说明：
// 界面功能表。每一行对应原版修改器里的一个条目：快捷键 / 功能 / 数值。
// 默认快捷键沿用原版：
//   1.24E V1.6  Ctrl+Q 英雄升级、Ctrl+X 瞬间移动 …… Ctrl+numeric 1~7、Home / End
//   1.20E V3.04 Ctrl+numeric 8/9、Down Arrow+numeric 重叠技能、Alt+E 全BUFF、Alt+D 变羊、Alt+numeric 1
//   1.27A 的功能原版没有快捷键，这里补上没有冲突的组合键。
#ifndef ROWS_H_INCLUDED_
#define ROWS_H_INCLUDED_

#include "stdafx.h"

enum RowKind {
	ROW_COMMAND = 0,    // 按一次执行一次
	ROW_TOGGLE = 1      // 常驻开关，按一次开、再按一次关
};

enum RowArg {
	ARG_NONE = 0,
	ARG_INT,            // 整数
	ARG_FLOAT,          // 小数
	ARG_CODE            // 4 字符代码（单位 / 技能 / 物品 / 科技）
};

enum RowGroup {
	GROUP_UNIT = 0,
	GROUP_HERO,
	GROUP_OVERLAP,
	GROUP_ITEM,
	GROUP_GAME,
	GROUP_TOGGLE,
	GROUP_COUNT
};

struct RowDef {
	RowKind kind;
	int id;                     // W3T_CommandId 或 W3T_ToggleId
	int group;                  // RowGroup
	const wchar_t* key;         // ini 键名（不要修改，否则用户的设置会丢失）
	const wchar_t* name;        // 显示名称
	const wchar_t* hotkey;      // 默认快捷键
	RowArg arg;                 // 数值类型
	const wchar_t* value;       // 默认数值
	DWORD needSupport;          // 需要的 W3T_SUPPORT_* 位（0 = 只需原生函数）
	const wchar_t* tip;         // 说明（状态栏显示）
};

extern const RowDef kRows[];
extern const int kRowCount;
extern const wchar_t* const kGroupNames[GROUP_COUNT];

#endif // ROWS_H_INCLUDED_
