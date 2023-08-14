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
