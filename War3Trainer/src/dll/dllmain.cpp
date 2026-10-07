// 模块说明：注入模块入口。DllMain 里只启动初始化线程，不做任何可能死锁的操作（加载器锁）。
#include "stdafx.h"
#include "Bridge.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(module);
		Bridge_Attach(module);
	}
	return TRUE;
}
