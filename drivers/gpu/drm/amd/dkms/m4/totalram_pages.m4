dnl #
dnl # v4.20-6506-gca79b0c211af
dnl # mm: convert totalram_pages and totalhigh_pages variables to atomic
dnl #
AC_DEFUN([AC_AMDGPU_TOTALRAM_PAGES], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/mm.h>
		], [
			unsigned long ret;
                        ret = totalram_pages();
		], [
			AC_DEFINE(HAVE_TOTALRAM_PAGES, 1,
				[totalram_pages() is available])
		])
	])
])
