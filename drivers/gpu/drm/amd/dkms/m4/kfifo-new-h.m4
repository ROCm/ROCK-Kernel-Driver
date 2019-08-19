dnl #
dnl # commit 4201d9a8e86b51dd40aa8a0dabd093376c859985
dnl #    kfifo: add the new generic kfifo API
dnl #
AC_DEFUN([AC_AMDGPU_KFIFO_NEW_H],
	[AC_MSG_CHECKING([whether kfifo-new.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([kfifo-new.h],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KFIFO_NEW_H, 1, [kfifo_new.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
