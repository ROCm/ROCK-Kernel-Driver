AC_DEFUN([AC_AMDGPU_MMU_NOTIFIER], [
dnl #
dnl # commit 4a83bfe916f3d2100df5bc8389bd182a537ced3e
dnl # mm/mmu_notifier: helper to test if a range invalidation is blockable
dnl #
	AC_MSG_CHECKING([whether mmu_notifier_range_blockable() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mmu_notifier.h>
	], [
		mmu_notifier_range_blockable(NULL);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MMU_NOTIFIER_RANGE_BLOCKABLE, 1, [
			mmu_notifier_range_blockable() is available
		])
	], [
		AC_MSG_RESULT(no)
	])
])
