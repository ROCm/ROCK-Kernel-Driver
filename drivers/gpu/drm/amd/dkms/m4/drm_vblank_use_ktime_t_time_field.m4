dnl #
dnl # commit v4.14-rc3-721-g67680d3c0464 dnl # drm: vblank: use ktime_t  instead of timeval
AC_DEFUN([AC_AMDGPU_DRM_VBLANK_USE_KTIME_T], [
	AC_KERNEL_DO_BACKGROUND([
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_vblank.h>
	], [
		struct drm_vblank_crtc *vblank = NULL;
		vblank->time = 0;
	], [
		AC_DEFINE(HAVE_DRM_VBLANK_USE_KTIME_T, 1,
			  [drm_vblank->time uses ktime_t type])
	])
	])
])
