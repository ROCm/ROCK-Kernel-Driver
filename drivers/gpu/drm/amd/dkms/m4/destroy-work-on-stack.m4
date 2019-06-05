dnl #
dnl # commit dc186ad741c12ae9ecac8b89e317ef706fdaf8f6
dnl # workqueue: Add debugobjects support
dnl #
AC_DEFUN([AC_AMDGPU_DESTROY_WORK_ON_STACK],
	[AC_MSG_CHECKING([whether destroy_work_on_stack() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/workqueue.h>
	],[
		destroy_work_on_stack(NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DESTROY_WORK_ON_STACK, 1, [destroy_work_on_stack() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
