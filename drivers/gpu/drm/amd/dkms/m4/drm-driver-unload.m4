dnl # commit 11b3c20bdd15d17382068be569740de1dccb173d
dnl # drm: Change the return type of the unload hook to void

AC_DEFUN([AC_AMDGPU_DRM_DRIVER_UNLOAD], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
			], [
				struct drm_driver *foo = NULL;
				int i = foo->unload(NULL);
				i++;
			], [
				AC_DEFINE(DRM_DRIVER_UNLOAD_RETURN_INT, 1,
					[drm_driver unload returns int])
			])
		])
	])
])
