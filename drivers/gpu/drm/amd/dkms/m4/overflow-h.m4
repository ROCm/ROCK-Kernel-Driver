dnl #
dnl # Check whether linux/overflow.h is available
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_OVERFLOW_H],
	[AC_MSG_CHECKING([whether linux/overflow.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/overflow.h
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_OVERFLOW_H, 1, [linux/overflow.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
