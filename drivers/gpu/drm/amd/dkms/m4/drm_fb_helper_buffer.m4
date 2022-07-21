dnl #
dnl # commit v4.18-rc3-610-gd536540f304c
dnl # drm/kms: move driver specific fb common code to helper functions (v2)
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_BUFFER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_fb_helper.h>
		], [
			struct drm_fb_helper *fb_helper = NULL;
			fb_helper->buffer = NULL;
		], [
			AC_DEFINE(HAVE_DRM_FB_HELPER_BUFFER, 1, [struct drm_fb_helper has buffer field])
		])
	])
])
