dnl #
dnl # commit 34c0fd540e79fb49ef9ce864dae1058cca265780
dnl # mm, dax, pmem: introduce pfn_t
dnl #
AC_DEFUN([AC_AMDGPU_PFN_TO_PFN_T],
	[AC_MSG_CHECKING([whether __pfn_to_pfn_t() function is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/pfn_t.h>
	],[
		__pfn_to_pfn_t(0, PFN_DEV);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PFN_TO_PFN_T, 1, [__pfn_to_pfn_t() function is available])
	],[
		AC_MSG_RESULT(no)
	])
])
