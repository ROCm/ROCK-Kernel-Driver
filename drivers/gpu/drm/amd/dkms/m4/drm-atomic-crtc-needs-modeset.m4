dnl #
dnl # commit 2465ff6217f1b63e194cfd57018fa42abe7fcdf0
dnl # Author: Daniel Vetter <daniel.vetter@ffwll.ch>
dnl # Date:   Thu Jun 18 09:58:55 2015 +0200
dnl # drm/atomic: Extract needs_modeset function
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_CRTC_NEEDS_MODESET],
	[AC_MSG_CHECKING([whether drm_atomic_crtc_needs_modeset() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_atomic.h>
	],[
		drm_atomic_crtc_needs_modeset(NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ATOMIC_CRTC_NEEDS_MODESET, 1, [drm_atomic_crtc_needs_modeset() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
