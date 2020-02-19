dnl #
dnl #commit 7e9e8927330672c0c1c2b6a83d82343ce283294a
dnl #Author: Kevin Wang <Kevin1.Wang@amd.com>
dnl #Date:   Mon Aug 13 17:21:10 2018 +0800
dnl #drm/amdkcl: [4.16] fix drm .last code, .output_poll_changed conflict
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_LASTCLOSE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <drm/drmP.h>
				#include <drm/drm_fb_helper.h>
			], [
				drm_fb_helper_lastclose(NULL);
			], [drm_fb_helper_lastclose], [drivers/gpu/drm/drm_fb_helper.c], [
				AC_DEFINE(HAVE_DRM_FB_HELPER_LASTCLOSE, 1,
					[whether drm_fb_helper_lastclose() is available])
			])
		], [
			AC_DEFINE(HAVE_DRM_FB_HELPER_LASTCLOSE, 1,
				[whether drm_fb_helper_lastclose() is available])
		])
	])
])
