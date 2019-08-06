dnl #
dnl # fence_array.h is available
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_FENCE_ARRAY_H],
	[AC_MSG_CHECKING([whether fence_array.h is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/fence_array.h>
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FENCE_ARRAY_H, 1, [fence_array.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
