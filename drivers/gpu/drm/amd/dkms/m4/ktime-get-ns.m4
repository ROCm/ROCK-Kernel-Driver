dnl #
dnl # commit 897994e32b2b0a41ce4222c3b38a05bd2d1ee9fa
dnl # timekeeping: Provide ktime_get[*]_ns() helpers
dnl #
AC_DEFUN([AC_AMDGPU_KTIME_GET_NS],
	[AC_MSG_CHECKING([whether ktime_get_ns() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/ktime.h>
		#include <linux/timekeeping.h>
	],[
		ktime_get_ns();
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KTIME_GET_NS, 1, [ktime_get_ns is available])
	],[
		AC_MSG_RESULT(no)
	])
])
