dnl #
dnl # drm: whether drm_fb_helper_ioctl() exists
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_IOCTL],
	[AC_MSG_CHECKING([whether drm_fb_helper_ioctl() exists])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_fb_helper.h>
	], [
		drm_fb_helper_ioctl(NULL, 0, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_FB_HELPER_IOCTL, 1, [drm_fb_helper_ioctl() exists])
	], [
		AC_MSG_RESULT(no)
	])
])
