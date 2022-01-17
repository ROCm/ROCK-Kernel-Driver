dnl #
dnl # v5.10-13-gf0dbd2bd1c22
dnl # mm: slab: provide krealloc_array()
dnl #
AC_DEFUN([AC_AMDGPU_KREALLOC_ARRAY], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/gfp.h>
			#include <linux/slab.h>
		], [
			void *p = krealloc_array(NULL, 0, 0, GFP_KERNEL);
			(void)p;
		], [
			AC_DEFINE(HAVE_KREALLOC_ARRAY, 1,
				[krealloc_array() is available])
		])
	])
])
