dnl #
dnl # commit ec8bf1942567bf0736314da9723e93bcc73c131f
dnl # drm/fb-helper: Fixup fill_info cleanup
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_FILL_INFO],
	[AC_MSG_CHECKING([whether drm_fb_helper_fill_info() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_fb_helper.h>
	], [
		drm_fb_helper_fill_info(NULL, NULL, NULL);
	], [drm_fb_helper_fill_info], [drivers/gpu/drm/drm_fb_helper.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_FB_HELPER_FILL_INFO, 1, [drm_fb_helper_fill_info() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
