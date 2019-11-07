dnl #
dnl #commit ab5b87a3201f2dd656986d2b1fe16409bd549fde
dnl #Author: Prike Liang <Prike.Liang@amd.com>
dnl #Date:   Thu Aug 2 21:28:59 2018 +0800
dnl #drm/amdkcl: [4.17] kcl for refactor pm_genpd_remove_device function
dnl #
AC_DEFUN([AC_AMDGPU_PM_GENPD_REMOVE_DEVICE],[
		AC_MSG_CHECKING([whether pm_genpd_remove_device() wants 2 arguments])
		AC_KERNEL_TRY_COMPILE([
				#include <linux/pm_domain.h>
		],[
				pm_genpd_remove_device(NULL, NULL);
		],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_PM_GENPD_REMOVE_DEVICE_2ARGS, 1, [pm_genpd_remove_device() wants 2 arguments])
		],[
				AC_MSG_RESULT(no)
		])
])

