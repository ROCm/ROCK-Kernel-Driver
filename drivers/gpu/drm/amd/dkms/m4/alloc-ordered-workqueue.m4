dnl #
dnl # commit 81dcaf6516d8b
dnl # workqueue: implement alloc_ordered_workqueue()
dnl #
AC_DEFUN([AC_AMDGPU_ALLOC_ORDERED_WORKQUEUE], [
	AC_MSG_CHECKING([whether alloc_ordered_workqueue() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/workqueue.h>
	], [
		alloc_ordered_workqueue(NULL, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ALLOC_ORDERED_WORKQUEUE, 1, [alloc_ordered_workqueue() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
