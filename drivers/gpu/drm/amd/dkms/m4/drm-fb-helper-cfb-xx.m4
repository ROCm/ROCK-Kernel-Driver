dnl #
dnl # commit 742547b73d27e7bce2d0dd0f1b95692436f30950
dnl # drm/fb_helper: Create wrappers for blit, copyarea and fillrect funcs
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_CFB_XX], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
				#include <drm/drm_fb_helper.h>
			], [
				drm_fb_helper_cfb_fillrect(NULL,NULL);
				drm_fb_helper_cfb_copyarea(NULL,NULL);
				drm_fb_helper_cfb_imageblit(NULL,NULL);
			], [
				AC_DEFINE(HAVE_DRM_FB_HELPER_CFB_XX, 1,
					[drm_fb_helper_cfb_{fillrect/copyarea/imageblit}() is available])
			])
		], [
			AC_DEFINE(HAVE_DRM_FB_HELPER_CFB_XX, 1,
				[drm_fb_helper_cfb_{fillrect/copyarea/imageblit}() is available])
		])
	])
])
