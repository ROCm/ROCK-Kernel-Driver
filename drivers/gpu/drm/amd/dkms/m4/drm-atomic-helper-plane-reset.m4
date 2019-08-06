dnl #
dnl # commit 7f4de521001f4ea705d505c9f91f58d0f56a0e6d
dnl # Author: Alexandru Gheorghe <alexandru-cosmin.gheorghe@arm.com>
dnl # Date:   Sat Aug 4 17:15:21 2018 +0100
dnl # drm/atomic: Add __drm_atomic_helper_plane_reset
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_HELPER_PLANE_RESET], [
	AC_MSG_CHECKING([whether __drm_atomic_helper_plane_reset() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_atomic_helper.h>
	], [
		__drm_atomic_helper_plane_reset(NULL, NULL);
	], [__drm_atomic_helper_plane_reset], [drivers/gpu/drm/drm_atomic_helper.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_PLANE_RESET, 1, [__drm_atomic_helper_plane_reset() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
