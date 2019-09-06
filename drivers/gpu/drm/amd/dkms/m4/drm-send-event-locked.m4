dnl #
dnl # commit fb740cf2492cc
dnl # drm: Create drm_send_event helpers
dnl #
AC_DEFUN([AC_AMDGPU_DRM_SEND_EVENT_LOCKED],
	[AC_MSG_CHECKING([whether drm_send_event_locked() function is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drmP.h>
	],[
		drm_send_event_locked(NULL, NULL);
	], [drm_send_event_locked], [drivers/gpu/drm/drm_file.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_SEND_EVENT_LOCKED, 1, [drm_send_event_locked() function is available])
	],[
		AC_MSG_RESULT(no)
	])
])
