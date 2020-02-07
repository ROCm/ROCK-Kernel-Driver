dnl #
dnl # commit f453ba0460742ad027ae0c4c7d61e62817b3e7ef
dnl # DRM: add mode setting support
dnl #
dnl # commit c2d88e06bcb98540bb83fac874574eaa4f320363
dnl # drm: Move the legacy kms disable_all helper to crtc helpers
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CRTC_HELPER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc_helper.h>
		], [
			drm_helper_force_disable_all(NULL);
		], [
			AC_DEFINE(HAVE_DRM_HELPER_FORCE_DISABLE_ALL, 1,
				[drm_helper_force_disable_all() is available])
		])
	])
])

