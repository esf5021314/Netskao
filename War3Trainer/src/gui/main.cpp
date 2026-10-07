// 模块说明：界面程序入口
#include "stdafx.h"
#include "MainWindow.h"
#include "Config.h"
#include "GameLink.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
	// 只允许运行一个修改器：再次打开时把已有窗口调到前台
	HANDLE mutex = CreateMutexW(NULL, TRUE, L"Local\\War3Trainer_SingleInstance");
	if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
		HWND existing = FindWindowW(L"War3TrainerMainWindow", NULL);
		if (existing) {
			ShowWindow(existing, SW_RESTORE);
			SetForegroundWindow(existing);
		}
		return 0;
	}

	INITCOMMONCONTROLSEX icc;
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
	InitCommonControlsEx(&icc);

	Config_Init();
	GameLink_Init();
	if (!MainWindow_Create(instance, showCommand)) {
		MessageBoxW(NULL, L"创建主窗口失败", TRAINER_TITLE, MB_OK | MB_ICONERROR);
		return 1;
	}

	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	if (mutex) CloseHandle(mutex);
	return 0;
}
