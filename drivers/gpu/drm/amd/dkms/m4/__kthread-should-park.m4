dnl #
dnl # introduced commit 0121805d9d2b1fff371e195c28e9b86ae38b5e47
dnl # kthread: Add __kthread_should_park()
dnl #
AC_DEFUN([AC_AMDGPU___KTHREAD_SHOULD_PARK],
	[AC_MSG_CHECKING([whether __kthread_should_park() is available])
	AC_KERNEL_CHECK_SYMBOL_EXPORT([__kthread_should_park],[kernel/kthread.c],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE___KTHREAD_SHOULD_PARK, 1, [__kthread_should_park() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
