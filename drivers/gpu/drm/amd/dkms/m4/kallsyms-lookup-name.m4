dnl #
dnl # commit f60d24d2ad04977b0bd9e3eb35dba2d2fa569af9
dnl # hw-breakpoints: Fix broken hw-breakpoint sample module
dnl #
AC_DEFUN([AC_AMDGPU_KALLSYMS_LOOKUP_NAME], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/kallsyms.h>
		],[
			kallsyms_lookup_name(NULL);
		],[
			AC_DEFINE(HAVE_KALLSYMS_LOOKUP_NAME, 1, [kallsyms_lookup_name is available])
		])
	])
])
