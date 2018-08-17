dnl #
dnl # commit v2.6.36-5889-g3e4d3af501cc
dnl # mm: stack based kmap_atomic()
dnl #
dnl # commit v3.5-713-g1285e4c8a751
dnl # highmem: remove the deprecated form of kmap_atomic
dnl #
AC_DEFUN([AC_AMDGPU_KMAP_ATOMIC], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/highmem.h>
		], [
			kmap_atomic(NULL);
		], [
			AC_DEFINE(HAVE_KMAP_ATOMIC_ONE_ARG, 1,
				[kmap_atomic() have one argument])
		])
	])
])
