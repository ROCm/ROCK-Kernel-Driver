dnl #
dnl #commit 8ced390c2b18364af35e3d3f080e06f8ea96be9a
dnl #Author: Al Viro <viro@zeniv.linux.org.uk>
dnl #Date:   Sun Jul 2 22:05:03 2017 -0400
dnl #define __poll_t, annotate constants
dnl #
AC_DEFUN([AC_AMDGPU_TYPE__POLL_T],
		[AC_MSG_CHECKING([whether __poll_t is available])
		AC_KERNEL_TRY_COMPILE([
				#include <uapi/linux/types.h>
		],[
				__poll_t mask = 0;
		],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_TYPE___POLL_T, 1, [__poll_t is available])
		],[
				AC_MSG_RESULT(no)
		])
])

