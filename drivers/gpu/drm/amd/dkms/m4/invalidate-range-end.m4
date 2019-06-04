dnl #
dnl # commit 5d6527a784f7a6d247961e046e830de8d71b47d1
dnl # Author: Jérôme Glisse <jglisse@redhat.com>
dnl # Date:   Fri Dec 28 00:38:05 2018 -0800
dnl # mm/mmu_notifier: use structure for invalidate_range_start/end callback
dnl # Patch series "mmu notifier contextual informations", v2.
dnl #
AC_DEFUN([AC_AMDGPU_INVALIDATE_RANGE_END], [
	AC_MSG_CHECKING([whether invalidate_range_end() wants 2 args])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mmu_notifier.h>
	], [
		struct mmu_notifier_ops *ops = NULL;
		ops->invalidate_range_end(NULL, NULL);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_INVALIDATE_RANGE_END, 1, whether invalidate_range_end() wants 2 args)
	], [
		AC_MSG_RESULT(no)
	])
])
