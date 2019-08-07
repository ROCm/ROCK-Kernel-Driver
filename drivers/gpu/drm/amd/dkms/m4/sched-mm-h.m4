dnl #
dnl # Check whether linux/sched/mm.h is available
dnl #
AC_DEFUN([AC_AMDGPU_SCHED_MM_H],
	[AC_MSG_CHECKING([whether linux/sched/mm.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/sched/mm.h
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SCHED_MM_H, 1, [linux/sched/mm.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
