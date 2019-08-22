dnl #
dnl # commit cfe63423d9be3e7020296c3dfb512768a83cd099
dnl # drm/fb-helper: Add drm_fb_helper_set_suspend_unlocked()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_SET_SUSPEND_UNLOCKED],
	[AC_MSG_CHECKING([whether drm_fb_helper_set_suspend_unlocked() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_fb_helper.h>
	],[
		drm_fb_helper_set_suspend_unlocked(NULL,0);
	],[drm_fb_helper_unregister_fbi],[drivers/gpu/drm/drm_fb_helper.c],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_FB_HELPER_SET_SUSPEND_UNLOCKED, 1, [drm_fb_helper_set_suspend_unlocked() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
