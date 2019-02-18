dnl #
dnl # commit a4232963757e62b3b97bbba07cb92c6d448f6f4b
dnl # driver-core: Move kobj_to_dev from genhd.h to device.h
dnl #
AC_DEFUN([AC_AMDGPU_KOBJ_TO_DEV], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/device.h>
		],[
			kobj_to_dev(NULL);
		],[
			AC_DEFINE(HAVE_KOBJ_TO_DEV, 1,
				[kobj_to_dev() is available])
		])
	])
])
