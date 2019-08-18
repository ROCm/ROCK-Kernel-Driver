dnl #
dnl # Check whether linux/sched/sched.h is available
dnl # commit e601757102cfd3eeae068f53b3bc1234f3a2b2e9
dnl # sched/headers: Prepare for new header dependencies before moving code to <linux/sched/clock.h>
dnl #
AC_DEFUN([AC_AMDGPU_SCHED_CLOCK_H],
	[AC_MSG_CHECKING([whether linux/sched/clock.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/sched/clock.h
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SCHED_CLOCK_H, 1, [linux/sched/clock.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
