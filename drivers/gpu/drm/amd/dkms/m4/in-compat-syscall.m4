dnl #
dnl # API added in_compat_syscall
dnl #
AC_DEFUN([AC_AMDGPU_IN_COMPAT_SYSCALL], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/compat.h>
		], [
			#if !defined(in_compat_syscall)
			in_compat_syscall();
			#endif
		],[
			AC_DEFINE(HAVE_IN_COMPAT_SYSCALL, 1,
				[in_compat_syscall is defined])
		])
	])
])
