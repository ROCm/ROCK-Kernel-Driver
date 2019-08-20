dnl # commit 11b3c20bdd15d17382068be569740de1dccb173d
dnl # drm: Change the return type of the unload hook to void

AC_DEFUN([AC_AMDGPU_DRM_DRIVER_UNLOAD],
		[AC_MSG_CHECKING([whether drm_driver->unload() return int])
		AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
		], [
				struct drm_driver *foo = NULL;
				int i = foo->unload(NULL);
				i++;
		], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(DRM_DRIVER_UNLOAD_RETURN_INT, 1, [drm_driver unload returns int])
		], [
				AC_MSG_RESULT(no)
		])
])
