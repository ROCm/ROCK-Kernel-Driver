dnl #
dnl # commit dbe7aa622db96b5cd601f59d09c4f00b98b76079
dnl # timekeeping: Provide y2038 safe accessor to the seconds portion of CLOCK_REALTIME
dnl #
AC_DEFUN([AC_AMDGPU_KTIME_GET_REAL_SECONDS_REAL], [
	AC_MSG_CHECKING([whether ktime_get_real_seconds() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/ktime.h>
		#include <linux/timekeeping.h>
	],[
		ktime_get_real_seconds();
	],[ktime_get_real_seconds],[kernel/time/timekeeping.c],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KTIME_GET_REAL_SECONDS, 1, [ktime_get_real_seconds() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([AC_AMDGPU_KTIME_GET_REAL_SECONDS],
	[AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_backport.h], [
		AC_MSG_CHECKING([whether ktime_get_real_seconds() is available in drm_backport.h])
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_backport.h>
		], [
			ktime_get_real_seconds();
		], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_KTIME_GET_REAL_SECONDS, 1, [ktime_get_real_seconds() is available in drm_backport.h])
		], [
			AC_MSG_RESULT(no)
			AC_AMDGPU_KTIME_GET_REAL_SECONDS_REAL
		])
	], [
		AC_AMDGPU_KTIME_GET_REAL_SECONDS_REAL
	])
])
