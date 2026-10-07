// 模块说明：
// 连接链路测试（控制台）。配合 tests/mock 下的模拟魔兽进程使用，检查：
//   查找窗口 → 注入 War3Trainer.dll → 共享内存 → 版本识别（模拟 1.24E）
//   → 窗口消息投递命令 → 结果回传（不在游戏中 / 读取游戏内存出错被拦截）
//   → 开关切换被拒绝（补丁处字节与原版不一致）→ 卸载模块
#include "../src/gui/stdafx.h"
#include "../src/gui/GameLink.h"
#include <stdio.h>

static int Failures = 0;

// 控制台按 UTF-8 输出中文
static const char* U8(const wchar_t* text) {
	static char buffers[4][512];
	static int index = 0;
	char* out = buffers[index++ & 3];
	WideCharToMultiByte(CP_UTF8, 0, text, -1, out, 512, NULL, NULL);
	return out;
}
#define CHECK(cond, ...) do { if (!(cond)) { ++Failures; printf("  [失败] " __VA_ARGS__); printf("\n"); } else { printf("  [通过] " __VA_ARGS__); printf("\n"); } } while (0)

static bool WaitReady(int ms) {
	for (int t = 0; t < ms; t += 250) {
		GameLink_Poll();
		if (GameLink_Info().phase == LINK_READY) return true;
		if (GameLink_Info().phase == LINK_ERROR) { printf("  错误：%s\n", U8(GameLink_Error())); return false; }
		Sleep(250);
	}
	return false;
}

static bool WaitCounter(W3T_Shared* shm, LONG before, int ms) {
	for (int t = 0; t < ms; t += 50) {
		if (shm->resultCounter != before) return true;
		Sleep(50);
	}
	return false;
}

int main() {
	SetConsoleOutputCP(CP_UTF8);
	GameLink_Init();
	printf("等待连接模拟游戏 ...\n");
	bool ready = WaitReady(30000);
	CHECK(ready, "连接成功（phase=%d）", GameLink_Info().phase);
	if (!ready) return 1;
	W3T_Shared* shm = GameLink_Shared();
	CHECK(shm->gameBuild == 6387, "识别版本 build=%lu 名称=%s", shm->gameBuild, U8(shm->versionName));
	CHECK((shm->supportFlags & W3T_SUPPORT_NATIVES) && (shm->supportFlags & W3T_SUPPORT_VERIFIED), "支持位 %08lX", shm->supportFlags);
	CHECK(shm->toggleSupport == 0x1F, "开关支持位 %08lX", shm->toggleSupport);

	// 定时器心跳
	LONG hb = shm->heartbeat;
	Sleep(800);
	CHECK(shm->heartbeat > hb, "主线程定时器在运行（心跳 %ld -> %ld）", hb, shm->heartbeat);
	CHECK(shm->inGame == 0, "模拟环境判定为不在游戏中（读取游戏内存出错被拦截，没有崩溃）");

	// 发送命令
	LONG before = shm->resultCounter;
	LONG seq = GameLink_SendCommand(CMD_TELEPORT, 0, 0.0f);
	CHECK(seq > 0, "发送命令 seq=%ld", seq);
	CHECK(WaitCounter(shm, before, 3000), "收到命令结果");
	CHECK(shm->resultSeq == seq && shm->resultCmd == CMD_TELEPORT, "结果对应命令 seq=%ld cmd=%ld", shm->resultSeq, shm->resultCmd);
	CHECK(shm->resultCode == RES_NOT_IN_GAME, "结果码=%ld 文字=%s", shm->resultCode, U8(shm->resultText));

	// 连发多条命令（环形缓冲）
	before = shm->resultCounter;
	for (int i = 0; i < 10; ++i) GameLink_SendCommand(CMD_KILL, 0, 0.0f);
	Sleep(1500);
	CHECK(shm->resultCounter - before == 10, "连发 10 条命令全部处理（%ld 条）", shm->resultCounter - before);

	// 开关：模拟的 Game.dll 里没有原版字节，应拒绝写入
	before = shm->resultCounter;
	GameLink_SetToggle(TGL_NO_DEFEAT, true, 0);
	CHECK(WaitCounter(shm, before, 3000), "收到开关结果");
	CHECK(shm->toggleState[TGL_NO_DEFEAT] == 0 && shm->toggleWant[TGL_NO_DEFEAT] == 0, "补丁被拒绝，开关保持关闭：%s", U8(shm->resultText));

	before = shm->resultCounter;
	GameLink_SetToggle(TGL_NOCD_NOMANA, true, 0);
	CHECK(WaitCounter(shm, before, 3000) && shm->toggleState[TGL_NOCD_NOMANA] == 1, "无CD无蓝耗开关：%s", U8(shm->resultText));

	// 卸载
	DWORD pid = GameLink_Info().pid;
	GameLink_Shutdown(true);
	Sleep(1500);
	wchar_t name[64];
	_snwprintf(name, 63, W3T_SHM_NAME_FMT, pid);
	HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
	CHECK(h == NULL, "卸载后共享内存已释放");
	if (h) CloseHandle(h);

	// 重新连接（会再次注入）
	GameLink_Init();
	ready = WaitReady(30000);
	CHECK(ready, "卸载后可以重新注入并连接");
	GameLink_Shutdown(true);

	printf(Failures ? "\n共 %d 项失败\n" : "\n全部通过\n", Failures);
	return Failures ? 1 : 0;
}
