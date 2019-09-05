dnl #
dnl # commit v5.2-rc5-8-g9285ec4c8b61
dnl # timekeeping: Use proper clock specifier names in functions
dnl #
AC_DEFUN([AC_AMDGPU_KTIME_GET_BOOTTIME_NS],
	[AC_MSG_CHECKING([whether ktime_get_boottime_ns() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/ktime.h>
	],[
		ktime_get_boottime_ns();
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KTIME_GET_BOOTTIME_NS, 1, [ktime_get_boottime_ns() is available])
	],[
		AC_MSG_RESULT(no)
dnl #
dnl # commit v3.16-rc5-76-g897994e32b2b
dnl # timekeeping: Provide ktime_get[*]_ns() helpers
dnl # amdkcl: leverage HAVE_KTIME_GET_NS
dnl #
	])
])
