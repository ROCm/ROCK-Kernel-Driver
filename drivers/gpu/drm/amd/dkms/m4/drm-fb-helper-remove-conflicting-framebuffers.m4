dnl #
dnl # commit 44adece57e2604cec8527a499b48e4d584ab53b8
dnl # drm/fb-helper: Add a dummy remove_conflicting_framebuffers
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_REMOVE_CONFLICTING_FRAMEBUFFERS],
	[AC_MSG_CHECKING([whether drm_fb_helper_remove_conflicting_framebuffers() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_fb_helper.h>
	],[
		drm_fb_helper_remove_conflicting_framebuffers(NULL, NULL, false);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_FRAMEBUFFERS, 1, [drm_fb_helper_remove_conflicting_framebuffers() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
