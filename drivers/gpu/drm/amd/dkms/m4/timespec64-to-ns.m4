dnl #
dnl # commit 361a3bf00582469877f8d18ff20f1efa6b781274
dnl # time64: Add time64.h header and define struct timespec64
dnl #
AC_DEFUN([AC_AMDGPU_TIMESPEC64_TO_NS],
	[AC_MSG_CHECKING([whether timespec64_to_ns is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/time64.h],[
			AC_KERNEL_TRY_COMPILE([
			        #include <linux/time64.h>
			],[
			        timespec64_to_ns(NULL);
			],[
			        AC_MSG_RESULT(yes)
			        AC_DEFINE(HAVE_TIMESPEC64_TO_NS, 1, [struct timespec64_to_ns is available]) ],[
			        AC_MSG_RESULT(no)
			])
		],[
		AC_MSG_RESULT(no)
	])
])
