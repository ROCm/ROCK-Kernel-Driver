dnl #
dnl # Check whether linux/sched/task.h is available
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_TASK_H],
	[AC_MSG_CHECKING([whether linux/sched/task.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/sched/task.h
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TASK_H, 1, [linux/sched/task.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
