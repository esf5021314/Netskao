// 模块说明：
// 与游戏的连接：查找魔兽窗口 → 打开进程 → 注入 War3Trainer.dll → 打开共享内存 → 投递命令。
// 界面每 0.5 秒调用一次 GameLink_Poll()，全程自动，不需要“连接游戏”按钮
// （与原版一致：先开修改器还是先开游戏都可以）。
#ifndef GAMELINK_H_INCLUDED_
#define GAMELINK_H_INCLUDED_

#include "stdafx.h"

enum LinkPhase {
	LINK_SEARCHING = 0,     // 没有找到魔兽窗口
	LINK_OPENING,           // 找到窗口，正在打开进程 / 等待 Game.dll
	LINK_INJECTING,         // 已注入，等待模块初始化
	LINK_READY,             // 已连接
	LINK_ERROR              // 出错（错误信息见 GameLink_Error），稍后自动重试
};

struct LinkInfo {
	LinkPhase phase;
	DWORD pid;
	HWND hwnd;              // 投递命令的魔兽窗口（魔兽重建窗口后由模块报告新窗口）
	wchar_t exeName[64];
	bool elevated;          // 修改器自身是否以管理员身份运行
	bool stalled;           // 已连接，但模块超过 10 秒没有心跳（读图卡住 / 游戏无响应）
	LONG session;           // 每次进入“已连接”状态 +1
	LONG resultBase;        // 进入“已连接”时模块的结果序号，之前的结果不再显示
};

void GameLink_Init();
void GameLink_Poll();
void GameLink_Shutdown(bool unloadModule);

const LinkInfo& GameLink_Info();
const wchar_t* GameLink_Error();
W3T_Shared* GameLink_Shared();          // 未连接时为 NULL

// 发送命令；返回命令序号，失败返回 0
LONG GameLink_SendCommand(int cmd, int iarg, float farg);
// 设置常驻开关
bool GameLink_SetToggle(int toggleId, bool want, int argument);
// 设置“游戏内显示提示”
void GameLink_SetInGameMessages(bool enable);

#endif // GAMELINK_H_INCLUDED_
