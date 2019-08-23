dnl #
dnl # introduced commit 2a1d446019f9a5983ec5a335b95e8593fdb6fa2e
dnl # kthread: Implement park/unpark facility
dnl # exported commit 18896451eaeee497ef5c397d76902c6376a8787d
dnl # kthread: export kthread functions
dnl #
AC_DEFUN([AC_AMDGPU_KTHREAD_PARK_XX],
	[AC_MSG_CHECKING([whether kthread_{park/unpark/parkme/should_park}() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/kthread.h>
	],[
		kthread_parkme();
		kthread_park(NULL);
		kthread_unpark(NULL);
		kthread_should_park();
	],[kthread_parkme kthread_park kthread_unpark kthread_should_park],[kernel/kthread.c],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KTHREAD_PARK_XX, 1, [kthread_{park/unpark/parkme/should_park}() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
