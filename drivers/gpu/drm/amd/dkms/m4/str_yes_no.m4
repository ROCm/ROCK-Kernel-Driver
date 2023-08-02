dnl #
dnl # commit ea4692c75e1c63926e4fb0728f5775ef0d733888
dnl # lib/string_helpers: Consolidate string helpers implementation 
dnl #
AC_DEFUN([AC_AMDGPU_STR_YES_NO], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/string_helpers.h>
		], [
			const char *str;
			str = str_yes_no(true);
		], [
			AC_DEFINE(HAVE_STR_YES_NO, 1,
				[str_yes_no() is defined])
		])
	])
])
