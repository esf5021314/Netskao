// 模块说明：
// 主窗口。布局仿原版 CE 修改器：
//   ┌──────────────────────────────────────────────┐
//   │ 横幅：标题 / 说明                              │
//   │ 游戏：war3.exe  版本 1.24E  状态：已连接        │
//   │ 选中单位：Hpal（玩家 1）英雄等级 3              │
//   ├──────────────────────────────────────────────┤
//   │ 快捷键 │ 功能                 │ 数值 │ 状态     │  ← 分组列表，一行一个功能
//   ├──────────────────────────────────────────────┤
//   │ □游戏内显示提示 □仅游戏激活时响应 □置顶 [关于] │
//   │ 状态栏：最近一次执行结果                        │
//   └──────────────────────────────────────────────┘
// 操作：
//   * 全部功能都可以用快捷键在游戏里直接触发（与原版一样，修改器窗口可以最小化）；
//   * 双击“功能”列 / 选中后按回车：执行该功能；
//   * 单击“数值”列：修改数值（回车确认，Esc 取消），立即保存到 ini；
//   * 单击“快捷键”列：按下新的组合键（Esc 取消，Backspace 清除），立即保存到 ini；
//   * 右键：执行 / 修改快捷键 / 恢复默认等。
#include "stdafx.h"
#include "MainWindow.h"
#include "Rows.h"
#include "Hotkey.h"
#include "Config.h"
#include "GameLink.h"
#include "resource.h"
#include <vector>

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------
enum StatusColor {
	COLOR_NORMAL = 0,
	COLOR_OK,
	COLOR_FAIL,
	COLOR_GRAY,
	COLOR_BUSY
};

struct RowState {
	HotkeyBinding hotkey;
	wchar_t value[32];
	wchar_t status[32];
	int statusColor;
	DWORD statusTick;       // 命令结果的显示时间，4 秒后清除
	bool supported;
};

static HINSTANCE Instance;
static HWND MainWnd, ListView, LblGame, LblSel, LblStatus;
static HWND ChkMessages, ChkActive, ChkTopMost, ChkUnload, BtnAbout;
static HWND EditBox = NULL;
static WNDPROC EditBoxOriginalProc = NULL;
static int EditRow = -1;
static int CaptureRow = -1;             // 正在修改快捷键的行（-1 = 没有）
static LONG CaptureGen = 0;             // 当前捕获的编号，旧捕获的结果消息据此丢弃
static std::vector<RowState> RowStates;
static HFONT FontNormal, FontBold, FontTitle, FontSubtitle;
static int Dpi = 96;
static LONG LastResultCounter = 0;      // 已显示到第几条结果
static LONG ResultSession = -1;         // 结果序号属于哪一次连接（见 LinkInfo::session）
static int PendingRows[W3T_CMD_RING * 2];      // 命令序号 -> 行号
static wchar_t StatusText[256];

#define TIMER_UI    1
#define TIMER_LINK  2

static inline int S(int value) { return MulDiv(value, Dpi, 96); }

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------
static void StatusSet(const wchar_t* format, ...) {
	va_list args;
	va_start(args, format);
	_vsnwprintf(StatusText, 255, format, args);
	va_end(args);
	StatusText[255] = 0;
	SetWindowTextW(LblStatus, StatusText);
}

// 只在文字变化时更新控件，避免状态区每次刷新都闪烁（旧版重制踩过的坑）
static void TextSetIfChanged(HWND hwnd, const wchar_t* text) {
	wchar_t old[512];
	GetWindowTextW(hwnd, old, 512);
	if (wcscmp(old, text) != 0) SetWindowTextW(hwnd, text);
}

static int ItemFromRow(int row) {
	LVFINDINFOW fi;
	ZeroMemory(&fi, sizeof(fi));
	fi.flags = LVFI_PARAM;
	fi.lParam = row;
	return (int)SendMessageW(ListView, LVM_FINDITEMW, (WPARAM)-1, (LPARAM)&fi);
}

static int RowFromItem(int item) {
	if (item < 0) return -1;
	LVITEMW it;
	ZeroMemory(&it, sizeof(it));
	it.mask = LVIF_PARAM;
	it.iItem = item;
	if (!SendMessageW(ListView, LVM_GETITEMW, 0, (LPARAM)&it)) return -1;
	return (int)it.lParam;
}

static void CellSet(int row, int column, const wchar_t* text) {
	int item = ItemFromRow(row);
	if (item < 0) return;
	wchar_t old[128] = L"";
	LVITEMW it;
	ZeroMemory(&it, sizeof(it));
	it.iSubItem = column;
	it.pszText = old;
	it.cchTextMax = 128;
	SendMessageW(ListView, LVM_GETITEMTEXTW, item, (LPARAM)&it);
	if (wcscmp(old, text) == 0) return;
	it.pszText = const_cast<wchar_t*>(text);
	SendMessageW(ListView, LVM_SETITEMTEXTW, item, (LPARAM)&it);
}

static void RowStatusSet(int row, const wchar_t* text, int color, bool timed) {
	if (row < 0 || row >= kRowCount) return;
	RowState& rs = RowStates[row];
	lstrcpynW(rs.status, text, 32);
	rs.statusColor = color;
	rs.statusTick = timed ? GetTickCount() : 0;
	CellSet(row, 3, rs.status);
	int item = ItemFromRow(row);
	if (item >= 0) SendMessageW(ListView, LVM_REDRAWITEMS, item, item);
}

static void HotkeyCellRefresh(int row) {
	wchar_t text[64];
	Hotkey_Format(RowStates[row].hotkey, text, 64);
	if (!text[0]) lstrcpyW(text, L"（无）");
	CellSet(row, 0, text);
}

static void BindingsPush() {
	std::vector<HotkeyBinding> list(kRowCount);
	for (int i = 0; i < kRowCount; ++i) list[i] = RowStates[i].hotkey;
	Hotkey_SetBindings(list);
}

static void FilterPush() {
	HotkeyFilter f;
	f.mainWindow = MainWnd;
	f.gamePid = GameLink_Info().pid;
	f.onlyWhenActive = SendMessageW(ChkActive, BM_GETCHECK, 0, 0) == BST_CHECKED;
	f.suppressed = EditBox != NULL || CaptureRow >= 0;
	Hotkey_SetFilter(f);
}

// 数值合法性检查，并换算成命令参数
static bool ValueParse(const RowDef& d, const wchar_t* text, int* iarg, float* farg) {
	*iarg = 0;
	*farg = 0.0f;
	wchar_t* end = NULL;
	switch (d.arg) {
	case ARG_NONE:
		return true;
	case ARG_INT: {
		long long v = wcstoll(text, &end, 10);
		if (end == text || *end || v < -2147483647LL || v > 2147483647LL) return false;
		*iarg = (int)v;
		return true;
	}
	case ARG_FLOAT: {
		double v = wcstod(text, &end);
		if (end == text || *end || !(v > -1e9 && v < 1e9)) return false;
		*farg = (float)v;
		return true;
	}
	case ARG_CODE: {
		if (wcslen(text) != 4) return false;
		char code[5];
		for (int i = 0; i < 4; ++i) {
			if (text[i] < 0x21 || text[i] > 0x7E) return false;
			code[i] = (char)text[i];
		}
		code[4] = 0;
		*iarg = (int)W3T_FourCC(code);
		return true;
	}
	}
	return false;
}

// ---------------------------------------------------------------------------
// 执行
// ---------------------------------------------------------------------------
static void RowExecute(int row) {
	if (row < 0 || row >= kRowCount) return;
	const RowDef& d = kRows[row];
	W3T_Shared* shm = GameLink_Shared();
	if (!shm) {
		StatusSet(L"%ls：还没有连接到游戏", d.name);
		return;
	}
	if (!RowStates[row].supported) {
		StatusSet(L"%ls：当前游戏版本 %ls 不支持", d.name, shm->versionName);
		RowStatusSet(row, L"不支持", COLOR_GRAY, false);
		return;
	}
	int iarg = 0;
	float farg = 0.0f;
	if (!ValueParse(d, RowStates[row].value, &iarg, &farg)) {
		StatusSet(L"%ls：数值“%ls”格式不正确", d.name, RowStates[row].value);
		RowStatusSet(row, L"数值错误", COLOR_FAIL, true);
		return;
	}
	if (d.kind == ROW_TOGGLE) {
		bool want = shm->toggleState[d.id] == 0;
		GameLink_SetToggle(d.id, want, iarg);
		RowStatusSet(row, L"切换中…", COLOR_BUSY, false);
		return;
	}
	LONG seq = GameLink_SendCommand(d.id, iarg, farg);
	if (!seq) {
		StatusSet(L"%ls：发送命令失败", d.name);
		RowStatusSet(row, L"失败", COLOR_FAIL, true);
		return;
	}
	PendingRows[seq % (W3T_CMD_RING * 2)] = row;
	RowStatusSet(row, L"执行中…", COLOR_BUSY, true);
}

// 按顺序锁读取第 n 条结果：复制前后 counter 都等于 n 才算完整（模块写入过程中 counter 为 0）
static bool ResultRead(W3T_Shared* shm, LONG n, W3T_ResultEntry* out) {
	W3T_ResultEntry* entry = &shm->results[(DWORD)n % W3T_RESULT_RING];
	if (InterlockedCompareExchange(&entry->counter, 0, 0) != n) return false;
	memcpy(out, entry, sizeof(*out));
	if (InterlockedCompareExchange(&entry->counter, 0, 0) != n) return false;
	out->text[159] = 0;
	return true;
}

static void ResultShow(const W3T_ResultEntry& result) {
	const wchar_t* text = result.text;
	int cmd = result.cmd;
	bool ok = result.code == RES_OK;

	int row = -1;
	if (cmd > 0) {
		int candidate = PendingRows[(DWORD)result.seq % (W3T_CMD_RING * 2)];
		if (candidate >= 0 && candidate < kRowCount && kRows[candidate].kind == ROW_COMMAND && kRows[candidate].id == cmd) row = candidate;
	} else if (cmd < 0) {
		int toggle = -cmd - 1;
		for (int i = 0; i < kRowCount; ++i) if (kRows[i].kind == ROW_TOGGLE && kRows[i].id == toggle) row = i;
	}
	if (row >= 0 && kRows[row].kind == ROW_TOGGLE) {
		StatusSet(L"%ls", text);         // 开关的结果文字里已经带了名称
	} else if (row >= 0) {
		StatusSet(L"%ls：%ls", kRows[row].name, text);
		if (kRows[row].kind == ROW_COMMAND) RowStatusSet(row, ok ? L"√ 成功" : L"× 失败", ok ? COLOR_OK : COLOR_FAIL, true);
	} else {
		StatusSet(L"%ls", text);
	}
}

// 读取模块发布的结果（环形结果区，两次刷新之间来了多条也不会丢）
static void ResultsPoll(W3T_Shared* shm) {
	if (!shm) return;
	const LinkInfo& info = GameLink_Info();
	if (info.session != ResultSession) {     // 新连接：连接之前的旧结果不显示
		ResultSession = info.session;
		LastResultCounter = info.resultBase;
	}
	LONG counter = InterlockedCompareExchange(&shm->resultCounter, 0, 0);
	if (counter - LastResultCounter <= 0) {
		LastResultCounter = counter;
		return;
	}
	LONG first = LastResultCounter + 1;
	if (counter - first >= W3T_RESULT_RING) first = counter - W3T_RESULT_RING + 1;     // 太旧的已被覆盖
	for (LONG n = first; n - counter <= 0; ++n) {
		W3T_ResultEntry result;
		if (ResultRead(shm, n, &result)) ResultShow(result);
	}
	LastResultCounter = counter;
}

// ---------------------------------------------------------------------------
// 刷新
// ---------------------------------------------------------------------------
static void SupportRefresh(W3T_Shared* shm) {
	for (int i = 0; i < kRowCount; ++i) {
		const RowDef& d = kRows[i];
		bool supported = true;
		if (shm) {
			if (d.needSupport && (shm->supportFlags & d.needSupport) != d.needSupport) supported = false;
			if (d.kind == ROW_TOGGLE && !(shm->toggleSupport & (1u << d.id))) supported = false;
		}
		RowState& rs = RowStates[i];
		if (rs.supported != supported) {
			rs.supported = supported;
			if (!supported) RowStatusSet(i, L"不支持", COLOR_GRAY, false);
			else RowStatusSet(i, L"", COLOR_NORMAL, false);
		}
	}
}

static void UiRefresh() {
	const LinkInfo& info = GameLink_Info();
	W3T_Shared* shm = GameLink_Shared();
	wchar_t line1[256], line2[256];
	const wchar_t* admin = info.elevated ? L"" : L"（修改器未以管理员身份运行）";

	switch (info.phase) {
	case LINK_SEARCHING:
		_snwprintf(line1, 255, L"未找到魔兽争霸 III —— 启动游戏后会自动连接%ls", admin);
		break;
	case LINK_OPENING:
		_snwprintf(line1, 255, L"找到游戏 %ls（PID %lu），等待游戏加载……%ls", info.exeName, info.pid, admin);
		break;
	case LINK_INJECTING:
		_snwprintf(line1, 255, L"找到游戏 %ls（PID %lu），正在加载修改器模块……", info.exeName, info.pid);
		break;
	case LINK_READY:
		_snwprintf(line1, 255, L"游戏：%ls（PID %lu）    版本：%ls%ls    状态：%ls", info.exeName, info.pid,
			shm->versionName, (shm->supportFlags & W3T_SUPPORT_VERIFIED) ? L"（已逐条核对）" : L"（未完全核对，部分功能可能无效）",
			info.stalled ? L"游戏暂时无响应（读图中？）" : L"已连接");
		break;
	default:
		_snwprintf(line1, 255, L"错误：%ls", GameLink_Error());
		break;
	}
	line1[255] = 0;

	if (!shm) {
		lstrcpyW(line2, L"全部功能都用快捷键操作；单击“快捷键”或“数值”列可以修改");
	} else if (!shm->inGame) {
		lstrcpyW(line2, L"不在游戏中 —— 进入地图后即可使用（换图后无需重新连接）");
	} else if (shm->humanPlayers > 1) {
		_snwprintf(line2, 255, L"多人游戏（%ld 名玩家）—— 修改器只用于单人游戏，功能已停用", shm->humanPlayers);
	} else if (shm->selValid) {
		wchar_t code[5];
		W3T_FourCCToText(shm->selTypeId, code);
		if (shm->selHeroLevel > 0) {
			_snwprintf(line2, 255, L"选中单位：%ls（玩家 %ld）    英雄等级 %ld", code, shm->selOwnerId + 1, shm->selHeroLevel);
		} else {
			_snwprintf(line2, 255, L"选中单位：%ls（玩家 %ld）", code, shm->selOwnerId + 1);
		}
	} else {
		lstrcpyW(line2, L"未选中单位 —— 单位类功能需要先在游戏里框选一个单位");
	}
	line2[255] = 0;
	TextSetIfChanged(LblGame, line1);
	TextSetIfChanged(LblSel, line2);

	SupportRefresh(shm);
	ResultsPoll(shm);

	// 常驻开关的状态列
	DWORD now = GetTickCount();
	for (int i = 0; i < kRowCount; ++i) {
		const RowDef& d = kRows[i];
		RowState& rs = RowStates[i];
		if (!rs.supported) continue;
		if (d.kind == ROW_TOGGLE) {
			if (!shm) {
				if (rs.statusColor != COLOR_NORMAL || rs.status[0]) RowStatusSet(i, L"", COLOR_NORMAL, false);
				continue;
			}
			bool on = shm->toggleState[d.id] != 0;
			bool pending = shm->toggleWant[d.id] != shm->toggleState[d.id];
			if (pending) continue;
			const wchar_t* text = on ? L"● 已开启" : L"关闭";
			int color = on ? COLOR_OK : COLOR_NORMAL;
			if (wcscmp(rs.status, text) != 0 || rs.statusColor != color) RowStatusSet(i, text, color, false);
		} else if (rs.statusTick && now - rs.statusTick > 4000) {
			RowStatusSet(i, L"", COLOR_NORMAL, false);
		}
	}
	FilterPush();
}

// ---------------------------------------------------------------------------
// 快捷键捕获的取消（编辑数值、弹出菜单、点到别处、窗口失去激活时调用）
// ---------------------------------------------------------------------------
static void CaptureCancel() {
	if (CaptureRow < 0) return;
	Hotkey_CancelCapture();
	int row = CaptureRow;
	CaptureRow = -1;
	CaptureGen = 0;
	HotkeyCellRefresh(row);
	FilterPush();
}

// ---------------------------------------------------------------------------
// 数值编辑
// ---------------------------------------------------------------------------
static void EditEnd(bool commit);

static LRESULT CALLBACK EditBoxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_GETDLGCODE:
		return DLGC_WANTALLKEYS;
	case WM_KEYDOWN:
		if (wp == VK_RETURN) { EditEnd(true); return 0; }
		if (wp == VK_ESCAPE) { EditEnd(false); return 0; }
		break;
	case WM_CHAR:
		if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
		break;
	case WM_KILLFOCUS:
		EditEnd(true);
		break;
	}
	return CallWindowProcW(EditBoxOriginalProc, hwnd, msg, wp, lp);
}

static void EditBegin(int row) {
	if (row < 0 || row >= kRowCount || kRows[row].arg == ARG_NONE) return;
	CaptureCancel();        // 否则在编辑框里输入的数字会被当成新快捷键
	if (EditBox) EditEnd(true);
	int item = ItemFromRow(row);
	if (item < 0) return;
	SendMessageW(ListView, LVM_ENSUREVISIBLE, item, FALSE);
	RECT rc;
	rc.top = 2;
	rc.left = LVIR_BOUNDS;
	SendMessageW(ListView, LVM_GETSUBITEMRECT, item, (LPARAM)&rc);
	EditRow = row;
	EditBox = CreateWindowExW(0, L"EDIT", RowStates[row].value,
		WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
		rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
		ListView, (HMENU)(INT_PTR)IDC_EDIT_VALUE, Instance, NULL);
	SendMessageW(EditBox, WM_SETFONT, (WPARAM)FontNormal, TRUE);
	SendMessageW(EditBox, EM_LIMITTEXT, 30, 0);
	EditBoxOriginalProc = (WNDPROC)SetWindowLongPtrW(EditBox, GWLP_WNDPROC, (LONG_PTR)EditBoxProc);
	SendMessageW(EditBox, EM_SETSEL, 0, -1);
	SetFocus(EditBox);
	StatusSet(L"修改“%ls”的数值：回车确认，Esc 取消", kRows[row].name);
	FilterPush();
}

static void EditEnd(bool commit) {
	if (!EditBox) return;
	HWND box = EditBox;
	int row = EditRow;
	EditBox = NULL;         // 先清空，防止 DestroyWindow 触发 WM_KILLFOCUS 时重入
	EditRow = -1;
	wchar_t text[32] = L"";
	GetWindowTextW(box, text, 32);
	DestroyWindow(box);
	if (commit && row >= 0) {
		const RowDef& d = kRows[row];
		// 去掉首尾空格
		wchar_t* p = text;
		while (*p == L' ') ++p;
		size_t n = wcslen(p);
		while (n && p[n - 1] == L' ') p[--n] = 0;
		int iarg;
		float farg;
		if (!ValueParse(d, p, &iarg, &farg)) {
			MessageBeep(MB_ICONWARNING);
			StatusSet(L"“%ls”不是有效的数值：%ls", p,
				d.arg == ARG_CODE ? L"需要 4 个字符的代码，例如 AInv" : d.arg == ARG_FLOAT ? L"需要数字，例如 0.5" : L"需要整数");
		} else {
			lstrcpynW(RowStates[row].value, p, 32);
			CellSet(row, 2, p);
			Config_SetString(L"Value", d.key, p);
			StatusSet(L"“%ls”的数值已改为 %ls（已保存）", d.name, p);
			// 已开启的“英雄最大等级”立即更新数值
			W3T_Shared* shm = GameLink_Shared();
			if (d.kind == ROW_TOGGLE && shm && shm->toggleState[d.id]) GameLink_SetToggle(d.id, true, iarg);
		}
	}
	SetFocus(ListView);
	FilterPush();
}

// ---------------------------------------------------------------------------
// 快捷键修改
// ---------------------------------------------------------------------------
static void CaptureBegin(int row) {
	if (row < 0 || row >= kRowCount) return;
	if (EditBox) EditEnd(true);
	CaptureCancel();
	LONG gen = Hotkey_BeginCapture();
	if (!gen) return;
	CaptureRow = row;
	CaptureGen = gen;
	CellSet(row, 0, L"请按键……");
	StatusSet(L"为“%ls”按下新的快捷键（可以是组合键）；Esc 取消，Backspace 清除", kRows[row].name);
	FilterPush();
}

static void HotkeyAssign(int row, const HotkeyBinding& binding) {
	RowStates[row].hotkey = binding;
	wchar_t text[64];
	Hotkey_Format(binding, text, 64);
	Config_SetString(L"Hotkey", kRows[row].key, text);
	HotkeyCellRefresh(row);
}

static void HotkeyApply(int row, const HotkeyBinding& binding);

static void CaptureFinish(WPARAM wp, LPARAM packed) {
	// 已取消，或者已经开始了另一次捕获：丢弃旧结果
	if (CaptureRow < 0 || HK_CAPTURE_GEN(wp) != CaptureGen) return;
	int row = CaptureRow;
	CaptureRow = -1;
	CaptureGen = 0;
	FilterPush();
	int kind = HK_CAPTURE_KIND(wp);
	if (kind == HK_CAPTURE_CANCEL) {
		HotkeyCellRefresh(row);
		StatusSet(L"已取消修改快捷键");
		return;
	}
	HotkeyBinding binding = { 0, 0, { 0, 0, 0 } };
	if (kind == HK_CAPTURE_SET) binding = Hotkey_Unpack(packed);
	HotkeyApply(row, binding);
}

// 设置某一行的快捷键（与其它行冲突时从其它行移除），保存并生效
static void HotkeyApply(int row, const HotkeyBinding& binding) {
	// 与其它行冲突：从其它行移除
	wchar_t moved[256] = L"";
	if (binding.count) {
		for (int i = 0; i < kRowCount; ++i) {
			if (i != row && Hotkey_Equal(RowStates[i].hotkey, binding)) {
				HotkeyBinding empty = { 0, 0, { 0, 0, 0 } };
				HotkeyAssign(i, empty);
				_snwprintf(moved, 255, L"（已从“%ls”移除）", kRows[i].name);
				moved[255] = 0;
			}
		}
	}
	HotkeyAssign(row, binding);
	BindingsPush();
	wchar_t text[64];
	Hotkey_Format(binding, text, 64);
	if (binding.count) StatusSet(L"“%ls”的快捷键改为 %ls%ls", kRows[row].name, text, moved);
	else StatusSet(L"已清除“%ls”的快捷键", kRows[row].name);
}

// ---------------------------------------------------------------------------
// 列表
// ---------------------------------------------------------------------------
static void ListCreate() {
	ListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
		0, 0, 100, 100, MainWnd, (HMENU)(INT_PTR)IDC_LISTVIEW, Instance, NULL);
	SendMessageW(ListView, WM_SETFONT, (WPARAM)FontNormal, TRUE);
	SendMessageW(ListView, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
		LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);

	static const struct { const wchar_t* title; int width; } kColumns[] = {
		{ L"快捷键", 120 }, { L"功能", 330 }, { L"数值", 100 }, { L"状态", 110 }
	};
	for (int i = 0; i < 4; ++i) {
		LVCOLUMNW col;
		ZeroMemory(&col, sizeof(col));
		col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		col.pszText = const_cast<wchar_t*>(kColumns[i].title);
		col.cx = S(kColumns[i].width);
		col.iSubItem = i;
		SendMessageW(ListView, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
	}

	SendMessageW(ListView, LVM_ENABLEGROUPVIEW, TRUE, 0);
	for (int g = 0; g < GROUP_COUNT; ++g) {
		LVGROUP group;
		ZeroMemory(&group, sizeof(group));
		group.cbSize = sizeof(group);
		group.mask = LVGF_HEADER | LVGF_GROUPID;
		group.pszHeader = const_cast<wchar_t*>(kGroupNames[g]);
		group.iGroupId = g;
		SendMessageW(ListView, LVM_INSERTGROUP, (WPARAM)-1, (LPARAM)&group);
	}

	for (int i = 0; i < kRowCount; ++i) {
		LVITEMW it;
		ZeroMemory(&it, sizeof(it));
		it.mask = LVIF_TEXT | LVIF_PARAM | LVIF_GROUPID;
		it.iItem = i;
		it.lParam = i;
		it.iGroupId = kRows[i].group;
		it.pszText = const_cast<wchar_t*>(L"");
		int item = (int)SendMessageW(ListView, LVM_INSERTITEMW, 0, (LPARAM)&it);
		(void)item;
		HotkeyCellRefresh(i);
		CellSet(i, 1, kRows[i].name);
		CellSet(i, 2, kRows[i].arg == ARG_NONE ? L"" : RowStates[i].value);
	}
}

static LRESULT ListCustomDraw(NMLVCUSTOMDRAW* cd) {
	switch (cd->nmcd.dwDrawStage) {
	case CDDS_PREPAINT:
		return CDRF_NOTIFYITEMDRAW;
	case CDDS_ITEMPREPAINT:
		return CDRF_NOTIFYSUBITEMDRAW;
	case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
		int row = (int)cd->nmcd.lItemlParam;
		if (row < 0 || row >= kRowCount) return CDRF_DODEFAULT;
		const RowState& rs = RowStates[row];
		COLORREF color = GetSysColor(COLOR_WINDOWTEXT);
		if (!rs.supported) {
			color = RGB(150, 150, 150);
		} else if (cd->iSubItem == 3) {
			switch (rs.statusColor) {
			case COLOR_OK: color = RGB(0, 140, 0); break;
			case COLOR_FAIL: color = RGB(200, 0, 0); break;
			case COLOR_GRAY: color = RGB(150, 150, 150); break;
			case COLOR_BUSY: color = RGB(0, 90, 200); break;
			default: break;
			}
		} else if (cd->iSubItem == 0) {
			color = RGB(0, 70, 160);
		}
		cd->clrText = color;
		return CDRF_NEWFONT;
	}
	}
	return CDRF_DODEFAULT;
}

static int HitTest(int* column) {
	DWORD pos = GetMessagePos();
	LVHITTESTINFO hit;
	ZeroMemory(&hit, sizeof(hit));
	hit.pt.x = (short)LOWORD(pos);
	hit.pt.y = (short)HIWORD(pos);
	ScreenToClient(ListView, &hit.pt);
	SendMessageW(ListView, LVM_SUBITEMHITTEST, 0, (LPARAM)&hit);
	if (column) *column = hit.iSubItem;
	return RowFromItem(hit.iItem);
}

static void ContextMenuShow(int row) {
	if (row < 0) return;
	CaptureCancel();
	HMENU menu = CreatePopupMenu();
	AppendMenuW(menu, MF_STRING, IDM_EXECUTE, kRows[row].kind == ROW_TOGGLE ? L"开启 / 关闭(&E)" : L"执行(&E)");
	AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(menu, MF_STRING, IDM_SET_HOTKEY, L"修改快捷键(&K)");
	AppendMenuW(menu, MF_STRING, IDM_CLEAR_HOTKEY, L"清除快捷键");
	AppendMenuW(menu, MF_STRING, IDM_DEFAULT_HOTKEY, L"恢复默认快捷键");
	if (kRows[row].arg != ARG_NONE) {
		AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
		AppendMenuW(menu, MF_STRING, IDM_EDIT_VALUE, L"修改数值(&V)");
		AppendMenuW(menu, MF_STRING, IDM_DEFAULT_VALUE, L"恢复默认数值");
	}
	POINT pt;
	GetCursorPos(&pt);
	int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, MainWnd, NULL);
	DestroyMenu(menu);
	switch (cmd) {
	case IDM_EXECUTE: RowExecute(row); break;
	case IDM_SET_HOTKEY: CaptureBegin(row); break;
	case IDM_CLEAR_HOTKEY: {
		HotkeyBinding empty = { 0, 0, { 0, 0, 0 } };
		HotkeyAssign(row, empty);
		BindingsPush();
		StatusSet(L"已清除“%ls”的快捷键", kRows[row].name);
		break;
	}
	case IDM_DEFAULT_HOTKEY: {
		HotkeyBinding binding;
		if (Hotkey_Parse(kRows[row].hotkey, binding)) HotkeyApply(row, binding);
		break;
	}
	case IDM_EDIT_VALUE: EditBegin(row); break;
	case IDM_DEFAULT_VALUE:
		lstrcpynW(RowStates[row].value, kRows[row].value, 32);
		CellSet(row, 2, RowStates[row].value);
		Config_SetString(L"Value", kRows[row].key, RowStates[row].value);
		StatusSet(L"“%ls”的数值已恢复为 %ls", kRows[row].name, RowStates[row].value);
		break;
	}
}

static LRESULT ListNotify(NMHDR* hdr) {
	switch (hdr->code) {
	case NM_CUSTOMDRAW:
		return ListCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(hdr));
	case NM_CLICK: {
		int column = -1;
		int row = HitTest(&column);
		if (row >= 0 && column == 0) { CaptureBegin(row); break; }
		CaptureCancel();        // 点到别处：放弃修改快捷键
		if (row >= 0 && column == 2 && kRows[row].arg != ARG_NONE) EditBegin(row);
		break;
	}
	case NM_DBLCLK: {
		int column = -1;
		int row = HitTest(&column);
		if (CaptureRow >= 0) break;     // 正在修改快捷键（双击“快捷键”列时）
		if (row >= 0 && column != 0 && column != 2) RowExecute(row);
		break;
	}
	case NM_RCLICK: {
		int row = HitTest(NULL);
		ContextMenuShow(row);
		break;
	}
	case LVN_KEYDOWN: {
		if (CaptureRow >= 0) break;     // 正在修改快捷键：按键只用于捕获，不执行功能
		NMLVKEYDOWN* kd = reinterpret_cast<NMLVKEYDOWN*>(hdr);
		int item = (int)SendMessageW(ListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
		int row = RowFromItem(item);
		if (row < 0) break;
		if (kd->wVKey == VK_RETURN) RowExecute(row);
		else if (kd->wVKey == VK_F2) EditBegin(row);
		break;
	}
	case LVN_ITEMCHANGED: {
		NMLISTVIEW* lv = reinterpret_cast<NMLISTVIEW*>(hdr);
		if ((lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED) && CaptureRow < 0 && !EditBox) {
			int row = RowFromItem(lv->iItem);
			if (row >= 0) StatusSet(L"%ls —— %ls", kRows[row].name, kRows[row].tip);
		}
		break;
	}
	case LVN_BEGINSCROLL:
		if (EditBox) EditEnd(true);
		break;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// 布局与绘制
// ---------------------------------------------------------------------------
static const int kBannerHeight = 64;

static void Layout() {
	RECT rc;
	GetClientRect(MainWnd, &rc);
	int w = rc.right, h = rc.bottom;
	int pad = S(8);
	int y = S(kBannerHeight) + S(6);
	MoveWindow(LblGame, pad, y, w - pad * 2, S(20), TRUE);
	y += S(22);
	MoveWindow(LblSel, pad, y, w - pad * 2, S(20), TRUE);
	y += S(24);
	int bottom = h - S(64);
	MoveWindow(ListView, pad, y, w - pad * 2, bottom - y, TRUE);
	int cy = bottom + S(6);
	int x = pad;
	MoveWindow(ChkMessages, x, cy, S(120), S(22), TRUE); x += S(124);
	MoveWindow(ChkActive, x, cy, S(270), S(22), TRUE); x += S(274);
	MoveWindow(ChkTopMost, x, cy, S(80), S(22), TRUE); x += S(84);
	MoveWindow(ChkUnload, x, cy, S(150), S(22), TRUE);
	MoveWindow(BtnAbout, w - pad - S(70), cy, S(70), S(24), TRUE);
	MoveWindow(LblStatus, pad, cy + S(28), w - pad * 2, S(22), TRUE);

	// 让“功能”列占满剩余宽度
	RECT lrc;
	GetClientRect(ListView, &lrc);
	int fixed = S(120) + S(100) + S(110) + GetSystemMetrics(SM_CXVSCROLL);
	int nameWidth = lrc.right - fixed + GetSystemMetrics(SM_CXVSCROLL) - S(4);
	if (nameWidth < S(200)) nameWidth = S(200);
	SendMessageW(ListView, LVM_SETCOLUMNWIDTH, 1, nameWidth);
	InvalidateRect(MainWnd, NULL, FALSE);
}

static void BannerPaint(HDC dc) {
	RECT rc;
	GetClientRect(MainWnd, &rc);
	int height = S(kBannerHeight);
	// 深红 -> 黑 的渐变（魔兽争霸风格）
	for (int i = 0; i < height; ++i) {
		int t = i * 255 / (height ? height : 1);
		HBRUSH br = CreateSolidBrush(RGB(90 - t * 70 / 255, 12 - t * 10 / 255, 8 - t * 6 / 255));
		RECT line = { 0, i, rc.right, i + 1 };
		FillRect(dc, &line, br);
		DeleteObject(br);
	}
	SetBkMode(dc, TRANSPARENT);
	HFONT old = (HFONT)SelectObject(dc, FontTitle);
	SetTextColor(dc, RGB(255, 204, 0));
	RECT title = { S(14), S(6), rc.right - S(14), S(38) };
	DrawTextW(dc, TRAINER_TITLE, -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
	SelectObject(dc, FontSubtitle);
	SetTextColor(dc, RGB(230, 220, 200));
	RECT sub = { S(16), S(38), rc.right - S(14), S(60) };
	DrawTextW(dc, L"大象修改器 C++ 重制版 · 合并 1.20E / 1.24E / 1.27A 功能 · 全部快捷键操作 · 仅限单人游戏", -1, &sub,
		DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
	SetTextColor(dc, RGB(200, 160, 60));
	RECT ver = { 0, S(6), rc.right - S(14), S(38) };
	DrawTextW(dc, TRAINER_VERSION, -1, &ver, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
	SelectObject(dc, old);
}

static HWND CheckboxCreate(const wchar_t* text, int id, bool checked) {
	HWND hwnd = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
		0, 0, 10, 10, MainWnd, (HMENU)(INT_PTR)id, Instance, NULL);
	SendMessageW(hwnd, WM_SETFONT, (WPARAM)FontNormal, TRUE);
	SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
	return hwnd;
}

static HWND LabelCreate(int id, HFONT font) {
	HWND hwnd = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS,
		0, 0, 10, 10, MainWnd, (HMENU)(INT_PTR)id, Instance, NULL);
	SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
	return hwnd;
}

static void AboutShow() {
	wchar_t text[2048];
	_snwprintf(text, 2047,
		L"%ls %ls\n"
		L"大象修改器 C++ 重制版\n\n"
		L"功能来源：\n"
		L"  · war3 1.24E修改器V1.6 / war3 1.20e修改器V3.04（脚本编写：瘸腿大象）\n"
		L"  · 魔兽争霸1.27终极版（思路提供：瘸腿的大象，更新：岚风）\n\n"
		L"使用方法：\n"
		L"  1. 启动魔兽争霸和本修改器（顺序随意，修改器会自动连接）；\n"
		L"  2. 进入地图，在游戏里框选一个单位；\n"
		L"  3. 按快捷键。复制、移动、创建物品等以鼠标所指的位置为准。\n"
		L"  小键盘快捷键（Num0 ~ Num9）需要打开 NumLock。\n\n"
		L"快捷键与数值保存在：\n  %ls\n\n"
		L"本修改器只用于单人游戏（与游戏自带作弊码一样）；检测到多人游戏时自动停用。",
		TRAINER_TITLE, TRAINER_VERSION, Config_Path());
	text[2047] = 0;
	MessageBoxW(MainWnd, text, L"关于", MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
static void SettingsLoad() {
	RowStates.resize(kRowCount);
	for (int i = 0; i < kRowCount; ++i) {
		const RowDef& d = kRows[i];
		RowState& rs = RowStates[i];
		ZeroMemory(&rs, sizeof(rs));
		rs.supported = true;
		Config_Ensure(L"Hotkey", d.key, d.hotkey);
		wchar_t text[64];
		Config_GetString(L"Hotkey", d.key, d.hotkey, text, 64);
		if (!Hotkey_Parse(text, rs.hotkey)) Hotkey_Parse(d.hotkey, rs.hotkey);
		if (d.arg != ARG_NONE) {
			Config_Ensure(L"Value", d.key, d.value);
			Config_GetString(L"Value", d.key, d.value, rs.value, 32);
			int iarg;
			float farg;
			if (!ValueParse(d, rs.value, &iarg, &farg)) lstrcpynW(rs.value, d.value, 32);
		}
	}
	for (int i = 0; i < W3T_CMD_RING * 2; ++i) PendingRows[i] = -1;
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_CREATE: {
		MainWnd = hwnd;
		LblGame = LabelCreate(IDC_LBL_GAME, FontBold);
		LblSel = LabelCreate(IDC_LBL_SEL, FontNormal);
		ListCreate();
		ChkMessages = CheckboxCreate(L"游戏内显示提示", IDC_CHK_MESSAGES, Config_GetInt(L"Setting", L"InGameMessages", 1) != 0);
		ChkActive = CheckboxCreate(L"仅在游戏 / 修改器窗口激活时响应快捷键", IDC_CHK_ACTIVE, Config_GetInt(L"Setting", L"OnlyWhenActive", 1) != 0);
		ChkTopMost = CheckboxCreate(L"窗口置顶", IDC_CHK_TOPMOST, Config_GetInt(L"Setting", L"TopMost", 0) != 0);
		ChkUnload = CheckboxCreate(L"退出时还原游戏", IDC_CHK_UNLOAD, Config_GetInt(L"Setting", L"UnloadOnExit", 1) != 0);
		BtnAbout = CreateWindowExW(0, L"BUTTON", L"关于", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_BTN_ABOUT, Instance, NULL);
		SendMessageW(BtnAbout, WM_SETFONT, (WPARAM)FontNormal, TRUE);
		LblStatus = LabelCreate(IDC_LBL_STATUS, FontNormal);
		GameLink_SetInGameMessages(SendMessageW(ChkMessages, BM_GETCHECK, 0, 0) == BST_CHECKED);
		if (SendMessageW(ChkTopMost, BM_GETCHECK, 0, 0) == BST_CHECKED) {
			SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}
		Layout();
		Hotkey_Start(hwnd);
		BindingsPush();
		FilterPush();
		SetTimer(hwnd, TIMER_UI, 200, NULL);
		SetTimer(hwnd, TIMER_LINK, 500, NULL);
		GameLink_Poll();
		UiRefresh();
		StatusSet(L"就绪。快捷键与数值保存在 War3Trainer.ini，可在列表里直接修改");
		return 0;
	}
	case WM_SIZE:
		if (ListView) Layout();
		return 0;
	case WM_GETMINMAXINFO: {
		MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lp);
		mmi->ptMinTrackSize.x = S(720);
		mmi->ptMinTrackSize.y = S(420);
		return 0;
	}
	case WM_ERASEBKGND: {
		HDC dc = (HDC)wp;
		RECT rc;
		GetClientRect(hwnd, &rc);
		rc.top = S(kBannerHeight);
		FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
		return 1;
	}
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		BannerPaint(dc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_TIMER:
		if (wp == TIMER_LINK) GameLink_Poll();
		else if (wp == TIMER_UI) UiRefresh();
		return 0;
	case WM_APP_HOTKEY: {
		int row = (int)wp;
		if (CaptureRow >= 0) return 0;      // 正在修改快捷键（轮询线程本身也不会投递）
		if (row >= 0 && row < kRowCount) RowExecute(row);
		return 0;
	}
	case WM_APP_HOTKEY_CAPTURED:
		CaptureFinish(wp, lp);
		return 0;
	case WM_ACTIVATE:
		// 切到游戏或其它窗口：放弃修改快捷键，免得在游戏里按的键被记成新快捷键
		if (LOWORD(wp) == WA_INACTIVE) CaptureCancel();
		break;
	case WM_NOTIFY: {
		NMHDR* hdr = reinterpret_cast<NMHDR*>(lp);
		if (hdr->hwndFrom == ListView) return ListNotify(hdr);
		break;
	}
	case WM_COMMAND:
		switch (LOWORD(wp)) {
		case IDC_CHK_MESSAGES: {
			bool on = SendMessageW(ChkMessages, BM_GETCHECK, 0, 0) == BST_CHECKED;
			Config_SetInt(L"Setting", L"InGameMessages", on ? 1 : 0);
			GameLink_SetInGameMessages(on);
			return 0;
		}
		case IDC_CHK_ACTIVE: {
			bool on = SendMessageW(ChkActive, BM_GETCHECK, 0, 0) == BST_CHECKED;
			Config_SetInt(L"Setting", L"OnlyWhenActive", on ? 1 : 0);
			FilterPush();
			return 0;
		}
		case IDC_CHK_TOPMOST: {
			bool on = SendMessageW(ChkTopMost, BM_GETCHECK, 0, 0) == BST_CHECKED;
			Config_SetInt(L"Setting", L"TopMost", on ? 1 : 0);
			SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			return 0;
		}
		case IDC_CHK_UNLOAD: {
			bool on = SendMessageW(ChkUnload, BM_GETCHECK, 0, 0) == BST_CHECKED;
			Config_SetInt(L"Setting", L"UnloadOnExit", on ? 1 : 0);
			return 0;
		}
		case IDC_BTN_ABOUT:
			AboutShow();
			return 0;
		}
		break;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY: {
		// 保存窗口大小（按 96 DPI 换算）
		WINDOWPLACEMENT wp2;
		wp2.length = sizeof(wp2);
		if (GetWindowPlacement(hwnd, &wp2) && wp2.showCmd == SW_SHOWNORMAL) {
			RECT r = wp2.rcNormalPosition;
			Config_SetInt(L"Setting", L"Width", MulDiv(r.right - r.left, 96, Dpi));
			Config_SetInt(L"Setting", L"Height", MulDiv(r.bottom - r.top, 96, Dpi));
		}
		KillTimer(hwnd, TIMER_UI);
		KillTimer(hwnd, TIMER_LINK);
		Hotkey_Stop();
		GameLink_Shutdown(SendMessageW(ChkUnload, BM_GETCHECK, 0, 0) == BST_CHECKED);
		PostQuitMessage(0);
		return 0;
	}
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

bool MainWindow_Create(HINSTANCE instance, int showCommand) {
	Instance = instance;
	HDC screen = GetDC(NULL);
	Dpi = GetDeviceCaps(screen, LOGPIXELSY);
	ReleaseDC(NULL, screen);
	if (Dpi < 96) Dpi = 96;

	FontNormal = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
	FontBold = CreateFontW(-S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
	FontTitle = CreateFontW(-S(24), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
	FontSubtitle = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

	SettingsLoad();

	WNDCLASSEXW wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = MainWndProc;
	wc.hInstance = instance;
	wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
	wc.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0);
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszClassName = L"War3TrainerMainWindow";
	if (!RegisterClassExW(&wc)) return false;

	int width = S(Config_GetInt(L"Setting", L"Width", 780));
	int height = S(Config_GetInt(L"Setting", L"Height", 780));
	// 不超过屏幕工作区（高 DPI / 小屏幕时，否则底部的选项会在屏幕外）
	int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
	POINT cursor = { 0, 0 };
	GetCursorPos(&cursor);
	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &mi)) {
		int workW = mi.rcWork.right - mi.rcWork.left;
		int workH = mi.rcWork.bottom - mi.rcWork.top;
		int maxW = workW - S(16), maxH = workH - S(16);
		if (width > maxW) width = maxW;
		if (height > maxH) height = maxH;
		if (width < S(720)) width = S(720);         // 与 WM_GETMINMAXINFO 的最小尺寸一致
		if (height < S(420)) height = S(420);
		// 接近整个工作区时，系统默认位置会让窗口底部出界：改为放在工作区左上角
		if (width > workW * 3 / 4 || height > workH * 3 / 4) {
			x = mi.rcWork.left + S(8);
			y = mi.rcWork.top + S(8);
		}
	}
	wchar_t title[128];
	_snwprintf(title, 127, L"%ls %ls（大象修改器 C++ 重制版）", TRAINER_TITLE, TRAINER_VERSION);
	title[127] = 0;
	HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		x, y, width, height, NULL, NULL, instance, NULL);
	if (!hwnd) return false;
	ShowWindow(hwnd, showCommand);
	UpdateWindow(hwnd);
	return true;
}
