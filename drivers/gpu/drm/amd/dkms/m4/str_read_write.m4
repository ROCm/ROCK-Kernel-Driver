dnl #
dnl # commit 1f5d7ea73c4b630dbb2c90818cb9fc0be54d2fe3
dnl # lib/string_helpers: Add str_read_write() helper
dnl # v6.0-rc1-122-g1f5d7ea73c4b
dnl #
AC_DEFUN([AC_AMDGPU_STR_READ_WRITE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/string_helpers.h>
		], [
			const char *str;
			str = str_read_write(true);
		], [
			AC_DEFINE(HAVE_STR_READ_WRITE, 1,
				[str_read_write() is defined])
		])
	])
])