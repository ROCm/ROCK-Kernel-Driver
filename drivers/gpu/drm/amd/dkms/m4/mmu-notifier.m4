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

		AC_AMDGPU_MMU_NOTIFIER_RANGE_FLAGS
	], [
		AC_MSG_RESULT(no)
		AC_AMDGPU_MMU_NOTIFIER_RANGE
	])
])

AC_DEFUN([AC_AMDGPU_MMU_NOTIFIER_RANGE], [
dnl #
dnl #
	AC_MSG_CHECKING([whether struct mmu_notifier_range is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mmu_notifier.h>
	], [
		struct mmu_notifier_range *ptest = NULL;
		ptest->start = 0;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MMU_NOTIFIER_RANGE, 1, [
			struct mmu_notifier_range is available
		])

		AC_AMDGPU_MMU_NOTIFIER_RANGE_FLAGS
	], [
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([AC_AMDGPU_MMU_NOTIFIER_RANGE_FLAGS], [
dnl #
dnl # commit 27560ee96f40017075bcb975b85f85dae3622f01
dnl # mm/mmu_notifier: convert mmu_notifier_range->blockable to a flags
dnl #
	AC_MSG_CHECKING([whether mmu_notifier_range->flags is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mmu_notifier.h>
	], [
		struct mmu_notifier_range *ptest = NULL;
		ptest->flags = MMU_NOTIFIER_RANGE_BLOCKABLE;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MMU_NOTIFIER_RANGE_FLAGS, 1, [
			mmu_notifier_range->flags is available
		])
	], [
		AC_MSG_RESULT(no)
	])
])

