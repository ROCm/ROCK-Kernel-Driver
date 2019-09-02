dnl #
dnl # commit c07dcd61a0e5
dnl # drm: Document device unplug infrastructure
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEV_UNPLUG],
	[AC_MSG_CHECKING([whether drm_dev_unplug() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		struct vm_area_struct;
		#include <drm/drm_drv.h>
	],[
		drm_dev_unplug(NULL);
	],[drm_dev_unplug],[drivers/gpu/drm/drm_drv.c],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DEV_UNPLUG, 1, [drm_dev_unplug() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
