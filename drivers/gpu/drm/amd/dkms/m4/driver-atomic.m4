dnl #
dnl # commit 88a48e297b3a3bac6022c03babfb038f1a886cea
dnl # drm: add atomic properties
dnl # commit 0e2a933b02c972919f7478364177eb76cd4ae00d
dnl # drm: Switch DRIVER_ flags to an enum
dnl #
AC_DEFUN([AC_AMDGPU_DRIVER_ATOMIC], [
	AC_MSG_CHECKING([whether DRIVER_ATOMIC is available])
        AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_drv.h>
        ], [
		int test = DRIVER_ATOMIC;
        ], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRIVER_ATOMIC, 1, [DRIVER_ATOMIC is available])
        ], [
		AC_MSG_RESULT(no)
        ])
])
