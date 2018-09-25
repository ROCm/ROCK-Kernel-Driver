dnl #
dnl # commit v4.7-rc2-610-gb3ac9f259106
dnl # drm: Extract drm_is_current_master
dnl # commit v4.7-rc2-612-g3b96a0b1407e
dnl # drm: document drm_auth.c
dnl #
AC_DEFUN([AC_AMDGPU_DRM_IS_CURRENT_MASTER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_auth.h], [
				AC_DEFINE(HAVE_DRM_IS_CURRENT_MASTER, 1,
					[drm_is_current_master() is available])
			], [
				AC_KERNEL_TRY_COMPILE([
					#include <drm/drmP.h>
				], [
					drm_is_current_master(NULL);
				], [
					AC_DEFINE(HAVE_DRM_IS_CURRENT_MASTER, 1,
						[drm_is_current_master() is available])
				])
			])
		], [
			AC_DEFINE(HAVE_DRM_IS_CURRENT_MASTER, 1,
				[drm_is_current_master() is available])
		])
	])
])
