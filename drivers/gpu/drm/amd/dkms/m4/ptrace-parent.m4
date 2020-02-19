dnl #
dnl # commit 06d984737bac0545fe20bb5447ee488b95adb531
dnl # ptrace: s/tracehook_tracer_task()/ptrace_parent()/
dnl #
AC_DEFUN([AC_AMDGPU_PTRACE_PARENT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/ptrace.h>
		], [
			ptrace_parent(NULL);
		], [
			AC_DEFINE(HAVE_PTRACE_PARENT, 1,
				[ptrace_parent() is available])
		])
	])
])
