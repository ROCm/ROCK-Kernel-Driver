dnl #
dnl # commit 1aabe902ca363
dnl # workqueue: introduce system_highpri_wq
dnl #
AC_DEFUN([AC_AMDGPU_SYSTEM_HIGHPRI_WQ], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_CHECK_SYMBOL_EXPORT([system_highpri_wq], [kernel/workqueue.c], [
			AC_DEFINE(HAVE_SYSTEM_HIGHPRI_WQ_EXPORTED, 1,
				[system_highpri_wq is exported])
			dnl #
			dnl # 73e4354444eef
			dnl # workqueue: declare system_highpri_wq
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <linux/workqueue.h>
			], [
				queue_work(system_highpri_wq, NULL);
			], [
				AC_DEFINE(HAVE_SYSTEM_HIGHPRI_WQ_DECLARED, 1,
					[system_highpri_wq is declared])
			])
		])
	])
])
