dnl #
dnl # commit v5.13-rc2-70-g70f400d4d957
dnl # driver core: Move the "removable" attribute from USB to core
dnl #
AC_DEFUN([AC_AMDGPU_DEV_IS_REMOVABLE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/device.h>
		], [
			bool res = 0;
			res = dev_is_removable(NULL);
		], [
			AC_DEFINE(HAVE_DEV_IS_REMOVABLE, 1,
				[dev_is_removable() is available])
		])
	])
])