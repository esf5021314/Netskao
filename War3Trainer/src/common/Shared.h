// ============================================================================
//  Shared.h —— 界面程序（War3Trainer.exe）与注入模块（War3Trainer.dll）之间的通信协议
//
//  通信方式（与原版大象修改器同一思路：界面只负责“下命令”，真正调用游戏函数的
//  代码运行在游戏主线程里）：
//
//    1. 注入模块在游戏进程内创建命名共享内存  Local\War3Trainer_Shm_<游戏进程PID>
//       界面程序打开同一块共享内存，双方都映射为 W3T_Shared 结构。
//    2. 界面把命令写进环形命令槽 ring[seq % W3T_CMD_RING]，然后
//       PostMessage(魔兽窗口, RegisterWindowMessage(W3T_MSG_NAME), W3T_WP_COMMAND, seq)
//    3. 注入模块子类化了魔兽窗口，在窗口过程（= 游戏主线程）里收到消息后执行命令，
//       把结果写回 resultSeq / resultText。
//
//  原版 CE 脚本是 Hook 一个每帧调用的函数来轮询“信箱”（tablekey）；这里改为窗口消息，
//  不需要为每个版本找 Hook 点，全版本通用。
// ============================================================================
#ifndef W3T_SHARED_H_INCLUDED_
#define W3T_SHARED_H_INCLUDED_

#include <windows.h>

#define W3T_ABI_VERSION        3u
#define W3T_SHARED_MAGIC       0x33543357u                  // 'W3T3'
#define W3T_SHM_NAME_FMT       L"Local\\War3Trainer_Shm_%lu" // %lu = 游戏进程 PID
#define W3T_MSG_NAME           L"War3Trainer.Command.v3"    // RegisterWindowMessage 名称
#define W3T_DLL_NAME           L"War3Trainer.dll"

// ---- 窗口消息的 wParam ------------------------------------------------------
enum W3T_WParam {
    W3T_WP_COMMAND      = 1,    // lParam = 命令序号 seq
    W3T_WP_SYNC_TOGGLES = 2,    // 按 toggleWant[] 同步常驻开关
    W3T_WP_UNLOAD       = 3,    // 还原所有补丁并卸载注入模块
    W3T_WP_PING         = 4     // 仅刷新状态
};

// ---- 命令编号（界面与注入模块共用，新增命令请追加到 CMD_COUNT 之前）------------
enum W3T_CommandId {
    CMD_NONE = 0,

    // 单位（需要先在游戏里框选一个单位）
    CMD_HERO_LEVELUP,       // 英雄升级            iarg = 升几级
    CMD_TELEPORT,           // 瞬间移动到鼠标位置
    CMD_KILL,               // 杀掉目标单位
    CMD_INVULNERABLE,       // 无敌
    CMD_VULNERABLE,         // 取消无敌
    CMD_RESET_COOLDOWN,     // 重置技能CD
    CMD_CLONE_SELF,         // 复制给自己（鼠标位置）
    CMD_CLONE,              // 复制单位（给原主人，鼠标位置）
    CMD_CLONE_MANY,         // 大量复制            iarg = 数量
    CMD_COPY_ITEMS,         // 复制背包物品到鼠标位置
    CMD_DROP_ITEMS,         // 丢弃所有物品
    CMD_SET_CHARGES,        // 物品数量（第1格）   iarg = 数量
    CMD_SET_SCALE,          // 设置大小            farg = 缩放
    CMD_FULL_CONTROL,       // 获得对方控制权
    CMD_PAUSE_UNIT,         // 暂停 / 恢复单位
    CMD_NO_COLLISION,       // 无视碰撞体积（开/关）
    CMD_KILL_PLAYER_UNITS,  // 秒杀该玩家的所有单位

    // 英雄 / 技能
    CMD_ADD_ABILITY,        // 添加技能            iarg = 技能代码
    CMD_REMOVE_ABILITY,     // 删除技能            iarg = 技能代码
    CMD_ALL_AURAS,          // 全光环
    CMD_ALL_PASSIVES,       // 全被动
    CMD_ALL_BUFFS,          // 全BUFF
    CMD_POLYMORPH,          // 变绵羊
    CMD_ADD_ATTRIBUTES,     // 增加属性（力/敏/智） iarg = 增加量
    CMD_SKILL_POINTS,       // 增加技能点数        iarg = 点数
    CMD_OVERLAP_ABILITY,    // 重叠技能（重复添加10次）iarg = 技能代码

    // 物品 / 资源 / 科技（不需要选中单位）
    CMD_GIVE_ARTIFACTS,     // 得到6个神器
    CMD_CREATE_ITEM,        // 得到物品            iarg = 物品代码
    CMD_CREATE_ALL_ITEMS,   // 创建所有物品
    CMD_MONEY_SELF,         // 设置钱和木（自己）  iarg = 数量
    CMD_MONEY_ALL,          // 设置钱和木（所有玩家）iarg = 数量
    CMD_SUMMON,             // 呼叫增援            iarg = 单位代码
    CMD_RESEARCH,           // 得到科技            iarg = 科技代码
    CMD_XP_RATE,            // 经验获取率          farg = 倍率

    // 游戏
    CMD_FOG_OFF,            // 关闭战争迷雾
    CMD_FOG_ON,             // 恢复战争迷雾
    CMD_PAUSE_GAME,         // 暂停 / 继续游戏

    CMD_COUNT
};

// ---- 常驻开关（再按一次关闭）-------------------------------------------------
enum W3T_ToggleId {
    TGL_NO_DEFEAT = 0,      // 不会失败
    TGL_FUN_MODE,           // 娱乐模式（快速建造 / 建筑可重叠 / 人口上限65535）
    TGL_AURA_STACK,         // 允许光环叠加
    TGL_MAX_HERO_LEVEL,     // 英雄最大等级（toggleArg = 等级）
    TGL_NOCD_NOMANA,        // 选中单位无CD无蓝耗
    TGL_COUNT
};
#define W3T_TOGGLE_SLOTS 16

// ---- 执行结果 -----------------------------------------------------------------
enum W3T_Result {
    RES_OK = 0,
    RES_NOT_IN_GAME,        // 不在游戏中（主菜单 / 读图）
    RES_NO_SELECTION,       // 没有选中单位
    RES_MULTIPLAYER,        // 多人对战，已拒绝
    RES_UNSUPPORTED,        // 当前版本不支持
    RES_FAILED,             // 执行失败
    RES_EXCEPTION           // 执行时发生异常（已拦截）
};

// ---- 注入模块状态 -------------------------------------------------------------
enum W3T_DllState {
    DLL_STARTING = 0,
    DLL_READY    = 1,
    DLL_FAILED   = 2,
    DLL_UNLOADED = 3
};

// ---- 支持程度位 ---------------------------------------------------------------
#define W3T_SUPPORT_NATIVES   0x01u   // JASS 原生函数表完整
#define W3T_SUPPORT_MOUSE     0x02u   // 鼠标世界坐标
#define W3T_SUPPORT_PATCHES   0x04u   // 代码补丁（不会失败 / 娱乐模式 / 光环叠加）
#define W3T_SUPPORT_MAXLEVEL  0x08u   // 英雄最大等级挂钩
#define W3T_SUPPORT_ITEMLIST  0x10u   // 物品数据表（创建所有物品）
#define W3T_SUPPORT_ABILINT   0x20u   // 内部添加技能函数（重叠技能）
#define W3T_SUPPORT_VERIFIED  0x80u   // 本版本已逐条反汇编核对

#define W3T_CMD_RING 32

#pragma pack(push, 4)

struct W3T_CmdSlot {
    volatile LONG seq;      // 写入后等于命令序号，模块据此判断槽位是否有效
    LONG  cmd;              // W3T_CommandId
    LONG  iarg;             // 整数参数（等级 / 数量 / 4字符代码）
    float farg;             // 浮点参数（缩放 / 倍率）
};

struct W3T_Shared {
    DWORD magic;
    DWORD abiVersion;
    DWORD structSize;

    // ---------------- 注入模块 -> 界面 ----------------
    volatile LONG dllState;             // W3T_DllState
    DWORD   gameBuild;                  // Game.dll 版本号末段（6387 = 1.24E）
    DWORD   gameBase;                   // Game.dll 基址
    DWORD   gameHwnd;                   // 魔兽主窗口
    DWORD   supportFlags;               // W3T_SUPPORT_*
    DWORD   toggleSupport;              // 位 i = 开关 i 在当前版本可用
    wchar_t versionName[16];            // “1.24E”
    wchar_t initError[128];             // 初始化失败原因

    volatile LONG heartbeat;            // 模块定时器每次 +1
    volatile LONG inGame;               // 1 = 正在游戏中
    volatile LONG humanPlayers;         // 游戏中的真人玩家数
    volatile LONG localPlayerId;        // 本地玩家编号

    volatile LONG selValid;             // 1 = 当前有选中单位
    DWORD   selTypeId;                  // 选中单位类型（4字符代码）
    LONG    selOwnerId;                 // 选中单位所属玩家编号
    LONG    selHeroLevel;               // 英雄等级（非英雄为 0）

    volatile LONG resultCounter;        // 每发布一条结果 +1（命令、开关切换、自动提示都会发布）
    volatile LONG resultSeq;            // 最近一次执行的命令序号
    LONG    resultCmd;                  // 最近一次执行的命令（负数 = 开关 -(TGL_xxx+1)，0 = 自动提示）
    LONG    resultCode;                 // W3T_Result
    wchar_t resultText[160];            // 结果说明

    volatile LONG toggleState[W3T_TOGGLE_SLOTS];   // 模块实际状态（1 = 已开启）

    // ---------------- 界面 -> 注入模块 ----------------
    volatile LONG cmdSeq;               // 界面最后写入的命令序号
    W3T_CmdSlot   ring[W3T_CMD_RING];
    volatile LONG toggleWant[W3T_TOGGLE_SLOTS];    // 界面期望状态
    volatile LONG toggleArg[W3T_TOGGLE_SLOTS];     // 开关参数（如英雄最大等级）
    volatile LONG inGameMessages;                  // 1 = 在游戏画面左侧显示执行结果

    DWORD reserved[32];
};

#pragma pack(pop)

// ---- 4字符代码 -----------------------------------------------------------------
//  魔兽的单位 / 技能 / 物品代码在 JASS 里是整数 'AInv' = 0x41496E76（大端顺序），
//  与原版 CE 脚本 `push 41496e76 //物品栏英雄` 的写法完全一致。
inline DWORD W3T_FourCC(const char* s) {
    return ((DWORD)(BYTE)s[0] << 24) | ((DWORD)(BYTE)s[1] << 16) |
           ((DWORD)(BYTE)s[2] << 8)  |  (DWORD)(BYTE)s[3];
}
inline void W3T_FourCCToText(DWORD id, wchar_t out[5]) {
    for (int i = 0; i < 4; ++i) {
        BYTE c = (BYTE)(id >> (24 - i * 8));
        out[i] = (c >= 0x20 && c < 0x7F) ? (wchar_t)c : L'?';
    }
    out[4] = 0;
}

#endif // W3T_SHARED_H_INCLUDED_
