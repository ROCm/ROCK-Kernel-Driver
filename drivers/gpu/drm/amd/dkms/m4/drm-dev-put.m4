dnl #
dnl # commit 9a96f55034e41b4e002b767e9218d55f03bdff7d
dnl # drm: introduce drm_dev_{get/put} functions
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEV_SUPPORTED],
	[AC_MSG_CHECKING([whether drm_dev_put() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		struct vm_area_struct;
		#include <drm/drm_drv.h>
	],[
		drm_dev_put(NULL);
	],[drm_dev_put],[drivers/gpu/drm/drm_drv.c],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DEV_PUT, 1, [drm_dev_put() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
