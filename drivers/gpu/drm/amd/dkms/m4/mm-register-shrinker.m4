dnl #
dnl # commit v3.11-8748-g1d3d4437eae1
dnl # vmscan: per-node deferred work
dnl #
AC_DEFUN([AC_AMDGPU_REGISTER_SHRINKER],
	[AC_MSG_CHECKING([whether register_shrinker() returns integer])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/mm.h>
	], [
		int ret;
		ret = register_shrinker(NULL);
	], [register_shrinker], [mm/vmscan.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REGISTER_SHRINKER_RETURN_INT, 1, [register_shrinker() returns integer])
	], [
AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_backport.h>
	], [
		int ret;
		ret = register_shrinker(NULL);
	], [register_shrinker], [mm/vmscan.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REGISTER_SHRINKER_RETURN_INT, 1, [register_shrinker() returns integer])
	], [
		AC_MSG_RESULT(no)
	])
	])
])
