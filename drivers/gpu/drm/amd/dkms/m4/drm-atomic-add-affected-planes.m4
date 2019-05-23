dnl #
dnl # commit e01e9f75a0c4e6cdbf1f139e37e9161408e49b7c
dnl # Author: Maarten Lankhorst <maarten.lankhorst@linux.intel.com>
dnl # Date:   Tue May 19 16:41:02 2015 +0200
dnl # drm/atomic: add drm_atomic_add_affected_planes
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_ADD_AFFECTED_PLANES],
	[AC_MSG_CHECKING([whether drm_atomic_add_affected_planes() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_atomic.h>
	], [
		drm_atomic_add_affected_planes(NULL, NULL);
	], [drm_atomic_add_affected_planes], [drivers/gpu/drm/drm_atomic.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ATOMIC_ADD_AFFECTED_PLANES, 1, [drm_atomic_add_affected_planes() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
