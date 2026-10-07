// 模块说明：
// 异常保护调用。往游戏进程里调用游戏内部函数，必须假设任何一次调用都可能出错
// （换图过程中句柄失效、地址表与版本不符等），出错时只让“这一条命令失败”，
// 而不是把玩家的游戏打崩。
//
//   MSVC   ：__try / __except
//   MinGW  ：在 fs:[0] 链上手工注册一个 SEH 帧（与 MSVC __try 的机制相同），
//            异常时用 __builtin_longjmp 跳回，并恢复 fs:[0]。
//            游戏函数内部自己的 SEH 帧在我们之后注册，所以游戏自己能处理的异常不受影响。
#ifndef SAFECALL_H_INCLUDED_
#define SAFECALL_H_INCLUDED_

#include <windows.h>
#include <type_traits>

typedef void (*SafeProc)(void* context);

// 调用 proc(context)；发生异常返回 false，并记录异常码与地址
bool SafeInvoke(SafeProc proc, void* context);

DWORD SafeLastExceptionCode();
void* SafeLastExceptionAddress();

// 便捷写法：SafeRun([&]{ ... });
template <typename F>
inline bool SafeRun(F&& fn) {
	typedef typename std::remove_reference<F>::type Fn;
	struct Thunk { static void Call(void* p) { (*static_cast<Fn*>(p))(); } };
	return SafeInvoke(&Thunk::Call, const_cast<void*>(static_cast<const void*>(&fn)));
}

#endif // SAFECALL_H_INCLUDED_
