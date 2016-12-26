dnl #
dnl # commit 8ef4227615e158faa4ee85a1d6466782f7e22f2f
dnl # x86/io: add interface to reserve io memtype for a resource range. (v1.1)
dnl #
AC_DEFUN([AC_AMDGPU_ARCH_IO_RESERVE_FREE_MEMTYPE_WC],
	[AC_MSG_CHECKING([whether arch_io_reserve_memtype_wc() and arch_io_free_memtype_wc() are available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/io.h>
	], [
		arch_io_reserve_memtype_wc(0, 0);
		arch_io_free_memtype_wc(0, 0);
	], [arch_io_reserve_memtype_wc arch_io_free_memtype_wc], [arch/x86/mm/pat.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ARCH_IO_RESERVE_FREE_MEMTYPE_WC, 1, [arch_io_reserve_memtype_wc() and arch_io_free_memtype_wc() are available])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether arch_io_reserve_memtype_wc() and arch_io_free_memtype_wc() are available in drm_backport.h])
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_backport.h>
		], [
			arch_io_reserve_memtype_wc(0, 0);
			arch_io_free_memtype_wc(0, 0);
		], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_ARCH_IO_RESERVE_FREE_MEMTYPE_WC, 1, [arch_io_reserve_memtype_wc() and arch_io_free_memtype_wc() are available in drm_backport.h])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
