dnl #
dnl # commit id:c46fd358070f22ba68d6e74c22016a33b914c20a
dnl # PCI/ASPM: Enable Latency Tolerance Reporting when supported
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_CANCEL_WORK], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/workqueue.h>
		], [
			cancel_work(NULL);
		], [cancel_work], [kernel/workqueue.c], [
			AC_DEFINE(HAVE_CANCEL_WORK, 1,
				[cancel_work() is available])
		])
	])
])

dnl #
dnl # commit id:v5.0-rc2-28-g8204e0c1113d
dnl # workqueue: Provide queue_work_node to queue work near a given NUMA node
dnl #
AC_DEFUN([AC_AMDGPU_QUEUE_WORK_NODE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/workqueue.h>
		], [
			queue_work_node(0,NULL, NULL);
		], [queue_work_node], [kernel/workqueue.c], [
			AC_DEFINE(HAVE_QUEUE_WORK_NODE, 1,
				[queue_work_node() is available])
		])
	])
])

AC_DEFUN([AC_AMDGPU_WORKQUEUE], [
	AC_AMDGPU_CANCEL_WORK
	AC_AMDGPU_QUEUE_WORK_NODE
])
