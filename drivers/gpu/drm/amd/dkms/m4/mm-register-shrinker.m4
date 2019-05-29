dnl #
dnl # commit 8e1f936b73150f5095448a0fee6d4f30a1f9001d
dnl # Author: Rusty Russell <rusty@rustcorp.com.au>
dnl # Date:   Tue Jul 17 04:03:17 2007 -0700
dnl # mm: clean up and kernelify shrinker registration
dnl #
AC_DEFUN([AC_AMDGPU_INT_REGISTER_SHRINKER],
	[AC_MSG_CHECKING([whether register_shrinker() returns integer])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/mm.h>
	], [
		int ret;
		ret = register_shrinker(NULL);
	], [register_shrinker], [mm/vmscan.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INT_REGISTER_SHRINKER, 1, [register_shrinker() returns integer])
	], [
		AC_MSG_RESULT(no)
	])
])
