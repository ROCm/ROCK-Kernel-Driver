dnl #
dnl #commit bc04f9fcf17ed940a4cd1bf102128bdfa8180873
dnl #Author: Kevin Wang <Kevin1.Wang@amd.com>
dnl #Date:   Mon Feb 26 17:03:21 2018 +0800
dnl #drm/amdkcl: [4.6] fix si_mem_available() compile error
dnl #
AC_DEFUN([AC_AMDGPU_SI_MEM_AVAILABLE],[
		AC_MSG_CHECKING([whether si_mem_available() is available])
		AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <linux/mm.h>
		], [
				si_mem_available();
		], [si_mem_available], [mm/page_alloc.c], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_SI_MEM_AVAILABLE, 1, [whether si_mem_available() is available])
		], [
				AC_MSG_RESULT(no)
		])
])

