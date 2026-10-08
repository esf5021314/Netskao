// 模块说明：注入模块与界面程序之间的桥，见 Bridge.cpp
#ifndef BRIDGE_H_INCLUDED_
#define BRIDGE_H_INCLUDED_

#include "stdafx.h"

// DllMain(DLL_PROCESS_ATTACH) 中调用，启动初始化线程
void Bridge_Attach(HMODULE self);

#endif // BRIDGE_H_INCLUDED_
