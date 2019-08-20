dnl #
dnl # Check whether linux/sched/signal.h is available
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_SIGNAL_H],
	[AC_MSG_CHECKING([whether linux/sched/signal.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/sched/signal.h
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SIGNAL_H, 1, [linux/sched/signal.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
