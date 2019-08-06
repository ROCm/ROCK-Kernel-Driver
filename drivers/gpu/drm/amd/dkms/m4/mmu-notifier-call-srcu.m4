dnl # commit b972216e27d1c853eced33f8638926636c606341
dnl # mmu_notifier: add call_srcu and sync function
dnl # for listener to delay call and sync
AC_DEFUN([AC_AMDGPU_MMU_NOTIFIER_CALL_SRCU],
	[AC_MSG_CHECKING([whether mmu_notifier_call_srcu() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mmu_notifier.h>
	],[
		mmu_notifier_call_srcu(NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MMU_NOTIFIER_CALL_SRCU, 1, [mmu_notifier_call_srcu() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
