dnl #
dnl # commit v5.5-rc5-1-g984cfe4e2526
dnl # mm/mmu_notifier: Rename struct mmu_notifier_mm to mmu_notifier_subscriptions
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_MMU_NOTIFIER_SUBSCRIPTIONS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/mmu_notifier.h>
		],[
			struct mm_struct *p = NULL;
			struct mmu_notifier_subscriptions *mmu_notifier = NULL;
			p->notifier_subscriptions = mmu_notifier;
		],[
			AC_DEFINE(HAVE_STRUCT_MMU_NOTIFIER_SUBSCRIPTIONS,
				1,
				[struct mmu_notifier_subscriptions is available])
		],[
			dnl #
			dnl # commit v5.4-rc5-19-g99cb252f5e68
			dnl # mm/mmu_notifier: add an interval tree notifier
			dnl #
			dnl # commit v5.4-rc5-18-g56f434f40f05
			dnl # mm/mmu_notifier: define the header pre-processor parts even if disabled
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <linux/spinlock.h>
				#include <linux/mmu_notifier.h>
			],[
				struct mmu_notifier_mm *p = NULL;
				spin_lock(&p->lock);
				spin_unlock(&p->lock);
			],[
				AC_DEFINE(HAVE_STRUCT_MMU_NOTIFIER_MM_EXPORTED,
				1,
				[struct mmu_notifier_mm is exported])
			])
		])
	])
])
