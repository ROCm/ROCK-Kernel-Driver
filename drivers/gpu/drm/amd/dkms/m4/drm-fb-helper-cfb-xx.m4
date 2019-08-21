dnl #
dnl # commit 742547b73d27e7bce2d0dd0f1b95692436f30950
dnl # drm/fb_helper: Create wrappers for blit, copyarea and fillrect funcs
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_CFB_XX],
	[AC_MSG_CHECKING([whether drm_fb_helper_cfb_{fillrect/copyarea/imageblit}() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_fb_helper.h>
	],[
		drm_fb_helper_cfb_fillrect(NULL,NULL);
		drm_fb_helper_cfb_copyarea(NULL,NULL);
		drm_fb_helper_cfb_imageblit(NULL,NULL);
	],[drm_fb_helper_cfb_fillrect drm_fb_helper_cfb_copyarea drm_fb_helper_cfb_imageblit],[drivers/gpu/drm/drm_fb_helper.c],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_FB_HELPER_CFB_XX, 1, [drm_fb_helper_cfb_{fillrect/copyarea/imageblit}() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
