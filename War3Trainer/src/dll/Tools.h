// 模块说明：
// 工具函数：取全局游戏对象、版本相关的结构字段访问、鼠标地图坐标、游戏内文字提示等。
// 函数命名与 TuringY Tools.h 保持一致（GameUIObjectGet / GameUIField ...）。
#ifndef TOOLS_H_INCLUDED_
#define TOOLS_H_INCLUDED_

#include "stdafx.h"
#include "GameStructs.h"
#include "Offsets.h"

// 当前 Game.dll 版本号末段（6387 = 1.24E）
inline DWORD GameVersionGet() { return Offset_GameVersion(); }

// CGameUI 版本差异：6328 之前（6048/6074/6263/6300）在 0x1B4 之后少 0xC 字节，
// 其后的字段整体前移 0xC。用法：GameUIField(ui->world)
template <typename T>
inline T& GameUIField(T& field) {
	return GameVersionGet() < 6328 ? *reinterpret_cast<T*>((uintptr_t)&field - 0xC) : field;
}

war3::CGameUI*   GameUIObjectGet();     // 游戏界面对象，不在游戏中时可能为 NULL
war3::CGameWar3* GameObjectGet();       // 全局游戏对象，主菜单时可能为 NULL

// 是否处于可以调用 JASS 原生函数的状态（游戏对象、界面对象、世界画面都已创建）
bool IsInGame();

// 鼠标所指的地图坐标；不在世界画面上或数值异常时返回 false
bool MouseWorldPosGet(float& x, float& y);

// 单位句柄 -> CUnit*（内部函数需要）
void* UnitObjectGet(uint32_t unitHandle);

// 在游戏画面左侧显示一行文字（UTF-8）；当前版本不支持时什么都不做
void TextPrint(float duration, const char* utf8Text);

// UTF-16 -> UTF-8
void WideToUtf8(const wchar_t* src, char* dst, int dstSize);

#endif // TOOLS_H_INCLUDED_
