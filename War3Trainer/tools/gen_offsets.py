# -*- coding: utf-8 -*-
"""
偏移表生成器

作用：
    从 TuringY 工程里已经整理好的偏移数据中，抽取本修改器用到的部分，生成
        src/dll/NativeOffsetIds.h   —— JASS 原生函数的偏移编号（#define NATIVE_xxx）
        src/dll/NativeOffsets.inc   —— 各版本的 OffsetSet(...) 语句
        src/dll/GlobalOffsets.inc   —— 全局对象 / 内部函数的 OffsetSet(...) 语句

数据来源：
    TuringY/Turing/native_offsets_<build>.inc    （JASS 原生函数，17 个版本）
    TuringY/Turing/Offsets.cpp                   （GLOBAL_UI 等全局地址）

用法：
    python gen_offsets.py <TuringY/Turing 目录> <War3Trainer/src/dll 目录>

说明：
    TuringY 的源文件是 GBK 编码，这里按 GBK 读取；生成的文件统一为 UTF-8（带 BOM），
    与本工程其它源文件保持一致，MSVC 与 MinGW 都能正确识别。
"""
import os
import re
import sys

# 本修改器用到的 JASS 原生函数（顺序即编号顺序，新增请追加到末尾，不要插队）
NATIVES = [
    # 单位组
    "CreateGroup", "DestroyGroup", "GroupEnumUnitsSelected", "GroupEnumUnitsOfPlayer",
    "FirstOfGroup", "GroupRemoveUnit",
    # 玩家
    "Player", "GetLocalPlayer", "GetOwningPlayer", "GetPlayerId", "GetPlayerController",
    "GetPlayerSlotState", "SetPlayerAlliance", "SetPlayerState", "GetPlayerState",
    "SetPlayerTechResearched", "SetPlayerHandicapXP",
    # 单位
    "CreateUnit", "KillUnit", "RemoveUnit", "SetUnitInvulnerable", "SetUnitPosition",
    "SetUnitScale", "GetUnitTypeId", "GetUnitFacing", "GetUnitX", "GetUnitY",
    "GetUnitState", "SetUnitState", "SetUnitVertexColor", "SetUnitPathing", "PauseUnit",
    "IsUnitPaused", "IsUnitType", "IssueTargetOrderById", "IssueImmediateOrderById",
    # 英雄
    "GetHeroLevel", "SetHeroLevel", "GetHeroStr", "GetHeroAgi", "GetHeroInt",
    "SetHeroStr", "SetHeroAgi", "SetHeroInt", "UnitModifySkillPoints",
    # 技能
    "UnitAddAbility", "UnitRemoveAbility", "SetUnitAbilityLevel", "UnitResetCooldown",
    # 物品
    "CreateItem", "GetItemTypeId", "SetItemCharges", "UnitItemInSlot",
    "UnitRemoveItemFromSlot", "UnitAddItemToSlotById",
    # 游戏
    "FogEnable", "FogMaskEnable", "PauseGame",
]
NATIVE_ID_BASE = 1000

# 从 TuringY Offsets.cpp 里抽取的全局地址 / 内部函数（TuringY 宏名 -> 本工程宏名）
GLOBALS = [
    ("GLOBAL_UI", "GLOBAL_GAMEUI"),                    # CGameUI** 全局指针
    ("GLOBAL_WARCRAFT_GAME", "GLOBAL_GAMEWAR3"),       # CGameWar3** 全局指针
    ("UNIT_FROM_HANDLE", "UNIT_FROM_HANDLE"),          # 句柄 -> CUnit*
    ("GET_HELPER_1", "UNIT_FROM_HANDLE_HELPER_1"),     # 6328 之前：句柄转换的前置调用 1
    ("GET_HELPER_2", "UNIT_FROM_HANDLE_HELPER_2"),     # 6328 之前：句柄转换的前置调用 2
    ("UI_TEXT_DISPLAY", "GAMEUI_TEXT_DISPLAY"),        # CGameUI::DisplayText
]

def read_gbk(path):
    with open(path, "rb") as f:
        return f.read().decode("gbk", errors="replace")

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    turing, out = sys.argv[1], sys.argv[2]

    # ---- 原生函数 ----
    builds = sorted(
        int(m.group(1))
        for m in (re.match(r"native_offsets_(\d+)\.inc$", n) for n in os.listdir(turing))
        if m
    )
    table = {}  # name -> {build: rva}
    for b in builds:
        text = read_gbk(os.path.join(turing, "native_offsets_%d.inc" % b))
        for m in re.finditer(r"JASS_NATIVE_(\w+)\s*=\s*\(JASS_PROTOTYPE_\w+\)\(base \+ (0x[0-9A-Fa-f]+)\)", text):
            table.setdefault(m.group(1), {})[b] = int(m.group(2), 16)

    with open(os.path.join(out, "NativeOffsetIds.h"), "w", encoding="utf-8-sig", newline="\n") as f:
        f.write("// 本文件由 tools/gen_offsets.py 生成，请勿手工修改\n")
        f.write("// JASS 原生函数的偏移编号，通过 Offset(NATIVE_xxx) 取得函数地址\n")
        f.write("#ifndef NATIVE_OFFSET_IDS_H_INCLUDED_\n#define NATIVE_OFFSET_IDS_H_INCLUDED_\n\n")
        for i, n in enumerate(NATIVES):
            f.write("#define NATIVE_%-28s %d\n" % (n, NATIVE_ID_BASE + i))
        f.write("\n#define NATIVE_ID_FIRST %d    // 第一个原生函数编号\n" % NATIVE_ID_BASE)
        f.write("#define NATIVE_ID_COUNT %d      // 原生函数个数\n" % len(NATIVES))
        f.write("\n#endif\n")

    with open(os.path.join(out, "NativeOffsets.inc"), "w", encoding="utf-8-sig", newline="\n") as f:
        f.write("// 本文件由 tools/gen_offsets.py 生成，请勿手工修改\n")
        f.write("// 数据来源：TuringY native_offsets_<build>.inc（共 %d 个版本）\n" % len(builds))
        for n in NATIVES:
            if n not in table:
                raise SystemExit("缺少原生函数: " + n)
            f.write("\n\t// %s\n" % n)
            for b in builds:
                if b in table[n]:
                    f.write("\tOffsetSet(NATIVE_%s, %d, 0x%06X);\n" % (n, b, table[n][b]))

    # ---- 全局地址 ----
    text = read_gbk(os.path.join(turing, "Offsets.cpp"))
    with open(os.path.join(out, "GlobalOffsets.inc"), "w", encoding="utf-8-sig", newline="\n") as f:
        f.write("// 本文件由 tools/gen_offsets.py 生成，请勿手工修改\n")
        f.write("// 数据来源：TuringY Offsets.cpp\n")
        for src, dst in GLOBALS:
            f.write("\n\t// %s（TuringY: %s）\n" % (dst, src))
            for m in re.finditer(r"^\s*OffsetSet\(%s,\s*(\d+),\s*(0x[0-9A-Fa-f]+)\)" % src, text, re.M):
                b, a = int(m.group(1)), int(m.group(2), 16)
                if a:
                    f.write("\tOffsetSet(%s, %d, 0x%06X);\n" % (dst, b, a))
    print("ok: %d 个原生函数, %d 个版本" % (len(NATIVES), len(builds)))
    return 0

if __name__ == "__main__":
    sys.exit(main())
