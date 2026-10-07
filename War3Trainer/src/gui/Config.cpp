// 模块说明：配置文件读写，见 Config.h
#include "stdafx.h"
#include "Config.h"

static wchar_t IniPath[MAX_PATH];

void Config_Init() {
	GetModuleFileNameW(NULL, IniPath, MAX_PATH);
	wchar_t* slash = wcsrchr(IniPath, L'\\');
	if (slash) slash[1] = 0; else IniPath[0] = 0;
	wcsncat(IniPath, L"War3Trainer.ini", MAX_PATH - wcslen(IniPath) - 1);
}

const wchar_t* Config_Path() { return IniPath; }

void Config_GetString(const wchar_t* section, const wchar_t* key, const wchar_t* def, wchar_t* out, int outSize) {
	GetPrivateProfileStringW(section, key, def, out, outSize, IniPath);
}

void Config_SetString(const wchar_t* section, const wchar_t* key, const wchar_t* value) {
	WritePrivateProfileStringW(section, key, value, IniPath);
}

int Config_GetInt(const wchar_t* section, const wchar_t* key, int def) {
	return (int)GetPrivateProfileIntW(section, key, def, IniPath);
}

void Config_SetInt(const wchar_t* section, const wchar_t* key, int value) {
	wchar_t text[32];
	_snwprintf(text, 31, L"%d", value);
	text[31] = 0;
	WritePrivateProfileStringW(section, key, text, IniPath);
}

void Config_Ensure(const wchar_t* section, const wchar_t* key, const wchar_t* def) {
	wchar_t buf[8];
	// 用一个不可能出现的默认值判断键是否存在
	GetPrivateProfileStringW(section, key, L"\x01", buf, 8, IniPath);
	if (buf[0] == 1 && buf[1] == 0) WritePrivateProfileStringW(section, key, def, IniPath);
}
