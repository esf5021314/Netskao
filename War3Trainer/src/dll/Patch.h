// 模块说明：
// 常驻开关类功能的代码补丁（对应原版 CE 修改器里独立的 [ENABLE]/[DISABLE] 脚本）。
//
// 安全措施：
//   * 写入前逐字节比对原始字节，不一致（被汉化补丁 / 其它插件改过）就拒绝写入；
//   * 关闭时只在当前字节等于我们写入的字节时才还原；
//   * 卸载模块时自动还原全部补丁。
#ifndef PATCH_H_INCLUDED_
#define PATCH_H_INCLUDED_

#include "stdafx.h"

// 当前版本是否有该开关的补丁数据
bool PatchSupported(int toggleId);

// 打开 / 关闭开关；失败时 reason 写入原因
bool PatchApply(int toggleId, bool enable, int argument, wchar_t* reason, int reasonSize);

// 该开关当前是否处于打开状态
bool PatchIsEnabled(int toggleId);

// 还原所有补丁（卸载时调用）
void PatchRestoreAll();

// ---- 每帧挂钩（见 Patch.cpp）----
// 挂上后，游戏每画一帧（CWorldFrameWar3 每帧函数）在游戏线程里调用一次 handler。
// 安装 / 卸下都用 8 字节原子写入，游戏正在运行时操作也安全。
typedef void (*FrameHandler)();
bool FrameHookInstall(FrameHandler handler, wchar_t* reason, int reasonSize);
// 卸下挂钩；挂钩点被其它程序改过时返回 false（此时不能卸载模块：存根还会调用模块里的代码）
bool FrameHookRemove();

#endif // PATCH_H_INCLUDED_
