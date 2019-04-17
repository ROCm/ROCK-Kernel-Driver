dnl #
dnl # commit a998e6220284191cd48dbb40d0d8b72a2a056e37
dnl # There is no member sequence in struct drm_pending_vblank_event before drm version(4.15.0)
dnl #
AC_DEFUN([AC_AMDGPU_SEQUENCE_IN_STRUCT_DRM_PENDING_VBLANK_EVENT],
	[AC_MSG_CHECKING([for sequence field in drm_pending_vblank_event structure])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_vblank.h>
	],[
		struct drm_pending_vblank_event e;
		e.sequence = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SEQUENCE_IN_STRUCT_DRM_PENDING_VBLANK_EVENT, 1, [there is sequence field in drm_pending_vblank_event structure])
	],[
		AC_MSG_RESULT(no)
	])
])
