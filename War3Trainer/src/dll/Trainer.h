// 模块说明：
// 命令执行层。两类调用方（由 Bridge.cpp 的锁串行化，Trainer 的状态只在持锁时访问）：
//   * 游戏线程（每帧挂钩）：Trainer_Execute / Trainer_Tick / Trainer_SyncToggles(gameThread = true) /
//     Trainer_Shutdown(gameThread = true)，可以调用 JASS 原生函数等游戏函数；
//   * 后台线程（对局画面没有刷新时）：Trainer_Reject / Trainer_Idle / Trainer_SyncToggles(false) /
//     Trainer_Shutdown(false)，只读内存、还原代码字节，不调用任何游戏函数。
#ifndef TRAINER_H_INCLUDED_
#define TRAINER_H_INCLUDED_

#include "stdafx.h"
#include "../common/Shared.h"

// 执行一条命令，结果写入结果环（游戏线程）
void Trainer_Execute(W3T_Shared* shm, const W3T_CmdSlot& slot);

// 对局画面没有刷新时代为回复命令（后台线程）；inGame = 对局还在（例如窗口最小化）
void Trainer_Reject(W3T_Shared* shm, const W3T_CmdSlot& slot, bool inGame);

// 按 shm->toggleWant[] 同步常驻开关。后台线程调用时如果对局还在（只是画面没有刷新），
// 不处理并返回 false，留给游戏线程
bool Trainer_SyncToggles(W3T_Shared* shm, bool gameThread);

// 约 100ms 一次（游戏线程）：刷新选中单位信息、维持“无CD无蓝耗”、清理施法单位、检查换图 / 多人游戏
void Trainer_Tick(W3T_Shared* shm);

// 约 100ms 一次（后台线程，对局画面没有刷新时）：只读内存检查是否还在对局中，
// 退出对局时还原代码补丁。返回是否还在对局中
bool Trainer_Idle(W3T_Shared* shm);

// 卸载前：还原补丁；在游戏线程调用时还会删除施法单位、取消修改器造成的暂停
void Trainer_Shutdown(W3T_Shared* shm, bool gameThread);

#endif // TRAINER_H_INCLUDED_
