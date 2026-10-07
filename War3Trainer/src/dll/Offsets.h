// 模块说明：
// 本头文件定义魔兽争霸 III 内部函数、全局对象等的偏移编号（Offset ID）。
// 写法与 TuringY 的 Offsets.h 一致：每个宏是一个编号，运行时通过 Offset(编号) 取得
// 当前版本下的真实地址（Game.dll 基址 + RVA），该版本未登记时返回 NULL。
//
// 各版本的 RVA 在 Offsets.cpp 中用 OffsetSet(编号, 版本号, RVA) 登记：
//   * JASS 原生函数        —— NativeOffsets.inc（tools/gen_offsets.py 从 TuringY 生成）
//   * 全局对象 / 内部函数  —— GlobalOffsets.inc（同上）+ Offsets.cpp 手工维护部分
#ifndef OFFSETS_H_INCLUDED_
#define OFFSETS_H_INCLUDED_

#include <windows.h>
#include "NativeOffsetIds.h"

bool  Offset_Init(DWORD version, DWORD base);   // 初始化偏移表，version 为 Game.dll 版本号末段（如 6387）
void* Offset(int offset);                       // 根据偏移编号取得地址，未登记返回 NULL
DWORD OffsetRva(int offset);                    // 根据偏移编号取得 RVA，未登记返回 0
DWORD Offset_GameVersion();                     // 当前版本号
DWORD Offset_GameBase();                        // 当前 Game.dll 基址

/*<! BEGIN Offset !>*/ // 偏移定义开始标志

// ---- 全局对象（GlobalOffsets.inc，来源 TuringY）----
#define GLOBAL_GAMEUI               1   // CGameUI**            全局游戏界面对象指针（TuringY: GLOBAL_UI）
#define GLOBAL_GAMEWAR3             2   // CGameWar3**          全局游戏对象指针（TuringY: GLOBAL_WARCRAFT_GAME）
#define UNIT_FROM_HANDLE            3   // 单位句柄 -> CUnit*   6328 起为 fastcall(handle)，之前为 thiscall(helper, handle)
#define UNIT_FROM_HANDLE_HELPER_1   4   // 6328 之前句柄转换的前置调用 1（TuringY: GET_HELPER_1）
#define UNIT_FROM_HANDLE_HELPER_2   5   // 6328 之前句柄转换的前置调用 2（TuringY: GET_HELPER_2）
#define GAMEUI_TEXT_DISPLAY         6   // CGameUI::DisplayText thiscall(ui, x, y, utf8, duration, -1)

// ---- 内部函数 / 数据（Offsets.cpp 手工维护，来源：原版 CE 脚本 + 反汇编核对）----
#define UNIT_ADD_ABILITY_INTERNAL   20  // CUnit 内部添加技能 fastcall(CUnit* ecx, 技能ID edx, 0, 0, 0)，ret 0Ch
                                        //   原版 CE 脚本 InGame_UnitAddAbitily（1.20E 5CC280 / 1.24E 24D900）
#define GLOBAL_ITEMDATA_TABLE       21  // war3::ItemDataHashTable 物品数据哈希表（创建所有物品）
#define GAME_MISC_GET_INT           22  // 读取 Misc 常量（整数）fastcall(段名 ecx, 键名 edx, 默认值)，ret 4
#define STR_MISC_MAXHEROLEVEL       23  // 字符串常量 "MaxHeroLevel"

/*<! END Offset !>*/ // 偏移定义结束标志

#endif // OFFSETS_H_INCLUDED_
