// 模块说明：模拟的 Game.dll（只有版本信息 1.24.4.6387），用于在没有魔兽的环境里测试注入与通信链路。
// 修改器模块会把它识别为 1.24E；读取游戏内部地址时会访问违例，正好用来检验异常保护是否生效。
#include <windows.h>
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
