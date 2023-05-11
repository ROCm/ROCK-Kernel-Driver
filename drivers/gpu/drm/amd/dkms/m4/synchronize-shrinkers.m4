dnl #
dnl # v5.14-rc3-760-g880121be1179
dnl # mm/vmscan: add sync_shrinkers function v3
dnl #
AC_DEFUN([AC_AMDGPU_SYNCHRONIZE_SHRINKERS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_CHECK_SYMBOL_EXPORT([synchronize_shrinkers],
			[mm/vmscan.c], [
			AC_DEFINE(HAVE_SYNCHRONIZE_SHRINKERS, 1,
				[synchronize_shrinkers() is available])
		])
	])
])
