dnl #
dnl # v5.13-105-gce6d42f2e4a2
dnl # mm: add vma_lookup(), update find_vma_intersection() comments
dnl #
AC_DEFUN([AC_AMDGPU_VMA_LOOKUP], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/mm.h>
		], [
			vma_lookup(NULL, 0);
		], [
			AC_DEFINE(HAVE_VMA_LOOKUP, 1,
				[vma_lookup() is available])
		])
	])
])
