// 模块说明：
// 命令执行层。所有函数都必须在游戏主线程调用（由 Bridge.cpp 的窗口过程转发）。
#ifndef TRAINER_H_INCLUDED_
#define TRAINER_H_INCLUDED_

#include "stdafx.h"
#include "../common/Shared.h"

// 执行一条命令，结果写入 shm->result*
void Trainer_Execute(W3T_Shared* shm, const W3T_CmdSlot& slot);

// 按 shm->toggleWant[] 同步常驻开关
void Trainer_SyncToggles(W3T_Shared* shm);

// 定时器（约 100ms 一次）：刷新选中单位信息、维持“无CD无蓝耗”、清理施法单位等
void Trainer_Tick(W3T_Shared* shm);

// 卸载前：还原补丁、清理施法单位
void Trainer_Shutdown(W3T_Shared* shm);

#endif // TRAINER_H_INCLUDED_
