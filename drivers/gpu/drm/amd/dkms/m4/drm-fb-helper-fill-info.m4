dnl #
dnl # commit ec8bf1942567bf0736314da9723e93bcc73c131f
dnl # drm/fb-helper: Fixup fill_info cleanup
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_FILL_INFO], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
				#include <drm/drm_fb_helper.h>
			], [
				drm_fb_helper_fill_info(NULL, NULL, NULL);
			], [
				AC_DEFINE(HAVE_DRM_FB_HELPER_FILL_INFO, 1,
					[drm_fb_helper_fill_info() is available])
			])
		], [
			AC_DEFINE(HAVE_DRM_FB_HELPER_FILL_INFO, 1,
				[drm_fb_helper_fill_info() is available])
		])
	])
])
