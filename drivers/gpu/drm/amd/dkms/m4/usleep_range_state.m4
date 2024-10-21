dnl #
dnl # v5.16-rc4-168-ge4779015fd5d
dnl # timers: implement usleep_idle_range()
dnl #
AC_DEFUN([AC_AMDGPU_USLEEP_RANGE_STATE], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE_SYMBOL([
                        #include <linux/delay.h>
			#include <linux/sched.h>
                ], [
	                usleep_range_state(0, 0, TASK_UNINTERRUPTIBLE);
		], [usleep_range_state], [kernel/time/timer.c], [
			AC_DEFINE(HAVE_USLEEP_RANGE_STATE, 1,
				[usleep_range_state() is available])
		])
        ])
])
