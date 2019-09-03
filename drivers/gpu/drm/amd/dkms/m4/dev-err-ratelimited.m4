dnl #
dnl # commit 6ca045930338485a8cdef117e74372aa1678009d
dnl # driver core: Add dev_*_ratelimited() family
dnl #
AC_DEFUN([AC_AMDGPU_DEV_ERR_RATELIMITED],
	[AC_MSG_CHECKING([whether dev_err_ratelimited() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/device.h>
	],[
		int test_val = 0;
		dev_err_ratelimited(NULL, "test value %d\n", test_val);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DEV_ERR_RATELIMITED, 1, [dev_err_ratelimited() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
