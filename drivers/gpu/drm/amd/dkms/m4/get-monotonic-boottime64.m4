dnl #
dnl # commit 2e0c78ee5ba4d777ecf22c8f40cc968b4308ca88
dnl # time: Expose get_monotonic_boottime64 for in-kernel use
dnl #
AC_DEFUN([AC_AMDGPU_GET_MONOTONIC_BOOTTIME64],
	[AC_MSG_CHECKING([whether get_monotonic_boottime64() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/timekeeping.h>
	],[
		get_monotonic_boottime64(NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_MONOTONIC_BOOTTIME64, 1, [get_monotonic_boottime is available])
	],[
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether get_monotonic_boottime64() in drm_backport.h is available])
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_backport.h>
		],[
			get_monotonic_boottime64(NULL);
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_GET_MONOTONIC_BOOTTIME64, 1, [get_monotonic_boottime in drm_backport.h is available])
		],[
			AC_MSG_RESULT(no)
		])
	])
])
