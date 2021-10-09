dnl #
dnl # commit v4.14-rc3-721-g67680d3c0464 dnl # drm: vblank: use ktime_t instead of timeval
AC_DEFUN([AC_AMDGPU_DRM_VBLANK_USE_KTIME_T], [
	AC_KERNEL_DO_BACKGROUND([
	AC_KERNEL_TRY_COMPILE([
		#ifdef HAVE_DRM_DRMP_H
		#include <drm/drmP.h>
		#else
		#include <drm/drm_vblank.h>
		#endif
		#include <linux/ktime.h>
	], [
		struct drm_vblank_crtc *vblank = NULL;
		vblank->time = ns_to_ktime(0);
	], [
		AC_DEFINE(HAVE_DRM_VBLANK_USE_KTIME_T, 1,
			  [drm_vblank->time uses ktime_t type])
	])
	])
])
