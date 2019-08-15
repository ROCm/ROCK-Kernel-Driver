dnl #
dnl # commit f519b1a2e08c913375324a927992bb328387f169
dnl # timekeeping: Provide ktime_get_raw()
dnl # Provide a ktime_t based interface for raw monotonic time.
dnl #
AC_DEFUN([AC_AMDGPU_KTIME_GET_RAW_NS],
	[AC_MSG_CHECKING([whether ktime_get_raw_ns() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/ktime.h>
		#include <linux/timekeeping.h>
	],[
		ktime_get_raw_ns();
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KTIME_GET_RAW_NS, 1, [ktime_get_raw_ns is available])
	],[
		AC_MSG_RESULT(no)
	])
])
