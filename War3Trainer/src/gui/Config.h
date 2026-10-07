// 模块说明：
// 配置文件 War3Trainer.ini（与 exe 同目录，第一次运行自动生成）
//   [Setting]  界面选项
//   [Hotkey]   每个功能的快捷键（键名见 Rows.cpp 的 key 列）
//   [Value]    每个功能的数值
#ifndef CONFIG_H_INCLUDED_
#define CONFIG_H_INCLUDED_

#include "stdafx.h"

void Config_Init();                         // 确定 ini 路径
const wchar_t* Config_Path();

void Config_GetString(const wchar_t* section, const wchar_t* key, const wchar_t* def, wchar_t* out, int outSize);
void Config_SetString(const wchar_t* section, const wchar_t* key, const wchar_t* value);
int  Config_GetInt(const wchar_t* section, const wchar_t* key, int def);
void Config_SetInt(const wchar_t* section, const wchar_t* key, int value);

// 键不存在时写入默认值（生成完整的 ini 方便用户手工修改）
void Config_Ensure(const wchar_t* section, const wchar_t* key, const wchar_t* def);

#endif // CONFIG_H_INCLUDED_
