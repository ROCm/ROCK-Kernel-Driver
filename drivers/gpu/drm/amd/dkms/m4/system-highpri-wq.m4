dnl #
dnl # commit 1aabe902ca363
dnl # workqueue: introduce system_highpri_wq
dnl #
AC_DEFUN([AC_AMDGPU_SYSTEM_HIGHPRI_WQ], [
	AC_MSG_CHECKING([whether system_highpri_wq is exported])
	AC_KERNEL_CHECK_SYMBOL_EXPORT([system_highpri_wq], [kernel/workqueue.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SYSTEM_HIGHPRI_WQ_EXPORTED, 1, [system_highpri_wq is exported])
		AC_MSG_CHECKING([whether system_highpri_wq is declared])
		dnl #
		dnl # 73e4354444eef
		dnl # workqueue: declare system_highpri_wq
		dnl #
		AC_KERNEL_TRY_COMPILE([
			#include <linux/workqueue.h>
		], [
			queue_work(system_highpri_wq, NULL);
		], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_SYSTEM_HIGHPRI_WQ_DECLARED, 1, [system_highpri_wq is declared])
		], [
			AC_MSG_RESULT(no)
		])
	], [
		AC_MSG_RESULT(no)
	])
])
