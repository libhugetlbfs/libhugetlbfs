#include <stdarg.h>
#include <asm/e2k_api.h>
#ifndef __ptr64__
#error
#endif
#define E2K_TRAP 3
int direct_syscall(int sysnum, ...) {
	long a0, a1, a2, a3, a4, a5, a6;
	va_list arg;
	va_start(arg, sysnum);
#define A(i) a##i = va_arg(arg, long);
	A(0) A(1) A(2) A(3) A(4) A(5) A(6)
#undef A
	return E2K_SYSCALL(E2K_TRAP, sysnum,
			7, a0, a1, a2, a3, a4, a5, a6);
}
