dnl #
dnl # commit v5.3-rc1-29-g2c7933f53f6b
dnl # mm/mmu_notifiers: add a get/put scheme for the registration
dnl #
AC_DEFUN([AC_AMDGPU_MMU_NOTIFIER_SYNCHRONIZE],
	[AC_MSG_CHECKING([whether mmu_notifier_synchronize() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mmu_notifier.h>
	],[
		mmu_notifier_synchronize();
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MMU_NOTIFIER_SYNCHRONIZE, 1, [mmu_notifier_synchronize() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
