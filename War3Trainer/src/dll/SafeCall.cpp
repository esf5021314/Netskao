// 模块说明：异常保护调用的实现，见 SafeCall.h
#include "stdafx.h"
#include "SafeCall.h"

static DWORD LastExceptionCode = 0;
static void* LastExceptionAddress = NULL;

DWORD SafeLastExceptionCode() { return LastExceptionCode; }
void* SafeLastExceptionAddress() { return LastExceptionAddress; }

#if defined(_MSC_VER)

static int SafeFilter(EXCEPTION_POINTERS* ep) {
	LastExceptionCode = ep->ExceptionRecord->ExceptionCode;
	LastExceptionAddress = ep->ExceptionRecord->ExceptionAddress;
	return EXCEPTION_EXECUTE_HANDLER;
}

bool SafeInvoke(SafeProc proc, void* context) {
	__try {
		proc(context);
		return true;
	}
	__except (SafeFilter(GetExceptionInformation())) {
		return false;
	}
}

#else // MinGW / Clang

#if !defined(__i386__)
#error "War3Trainer.dll 必须编译为 32 位（魔兽争霸 III 是 32 位程序）"
#endif

// fs:[0] 上的 SEH 注册记录，前两个字段的布局由操作系统规定
struct SafeSehRecord {
	SafeSehRecord*	prev;			// 上一个注册记录
	void*			handler;		// 异常处理函数
	void*			jumpBuffer[5];	// __builtin_setjmp 缓冲区
};

static inline SafeSehRecord* SehHeadGet() {
	SafeSehRecord* head;
	__asm__ __volatile__("movl %%fs:0, %0" : "=r"(head));
	return head;
}

static inline void SehHeadSet(SafeSehRecord* head) {
	__asm__ __volatile__("movl %0, %%fs:0" : : "r"(head) : "memory");
}

extern "C" EXCEPTION_DISPOSITION __cdecl SafeSehHandler(EXCEPTION_RECORD* record, void* establisherFrame,
	CONTEXT* /*context*/, void* /*dispatcherContext*/) {
	// 展开阶段不处理，只在首次分发时接管
	if (record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) {
		return ExceptionContinueSearch;
	}
	LastExceptionCode = record->ExceptionCode;
	LastExceptionAddress = record->ExceptionAddress;
	SafeSehRecord* frame = static_cast<SafeSehRecord*>(establisherFrame);
	__builtin_longjmp(frame->jumpBuffer, 1);
	return ExceptionContinueSearch;	// 不会执行到这里
}

__attribute__((noinline)) bool SafeInvoke(SafeProc proc, void* context) {
	SafeSehRecord frame;
	frame.prev = SehHeadGet();
	frame.handler = reinterpret_cast<void*>(&SafeSehHandler);
	if (__builtin_setjmp(frame.jumpBuffer) != 0) {
		// 从异常处理函数跳回：被调用函数内部注册的 SEH 帧已失效，恢复到进入前的链头
		SehHeadSet(frame.prev);
		return false;
	}
	SehHeadSet(&frame);
	proc(context);
	SehHeadSet(frame.prev);
	return true;
}

#endif
