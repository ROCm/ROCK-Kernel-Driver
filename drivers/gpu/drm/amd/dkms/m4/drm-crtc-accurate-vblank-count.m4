dnl #
dnl # commit ca814b25538a5b2c0a8de6665191725f41608f2c
dnl # Author: Daniel Vetter <daniel.vetter@ffwll.ch>
dnl # Date:   Wed May 24 16:51:47 2017 +0200
dnl # drm/vblank: Consistent drm_crtc_ prefix
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CRTC_ACCURATE_VBLANK_COUNT], [
	AC_MSG_CHECKING([whether drm_crtc_accurate_vblank_count() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <drm/drm_vblank.h>
	], [
		drm_crtc_accurate_vblank_count(NULL);

	], [drm_crtc_accurate_vblank_count], [drivers/gpu/drm/drm_vblank.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_CRTC_ACCURATE_VBLANK_COUNT, 1, [drm_crtc_accurate_vblank_count() is available])
	], [
		dnl #
		dnl # commit af61d5ce1532191213dce2404f9c45d32260a6cd
		dnl # Author: Maarten Lankhorst <maarten.lankhorst@linux.intel.com>
		dnl # Date:   Tue May 17 15:07:44 2016 +0200
		dnl # drm/core: Add drm_accurate_vblank_count, v5.
		dnl #
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether drm_accurate_vblank_count() is available in drm_irq.c])
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_irq.h>
		], [
			drm_accurate_vblank_count(NULL);

		], [drm_accurate_vblank_count], [drivers/gpu/drm/drm_irq.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_DRM_ACCURATE_VBLANK_COUNT, 1, [drm_accurate_vblank_count() is available in drm_irq.c])
		], [
			dnl #
			dnl # commit 3ed4351a83ca05d3cd886ade6900be1067aa7903
			dnl # Author: Daniel Vetter <daniel.vetter@ffwll.ch>
			dnl # Date:   Wed May 31 11:21:46 2017 +0200
			dnl # drm: Extract drm_vblank.[hc]
			dnl # drm_irq.c contains both the irq helper library (optional) and the
			dnl # vblank support (optional, but part of the modeset uapi, and doesn't)
			dnl # require the use of the irq helpers at all.
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING([whether drm_accurate_vblank_count() is available in drm_irq.c])
			AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <drm/drm_vblank.h>
			], [
				drm_accurate_vblank_count(NULL);
			], [drm_accurate_vblank_count], [drivers/gpu/drm/drm_vblank.c],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_DRM_ACCURATE_VBLANK_COUNT, 1, [drm_accurate_vblank_count() is available in drm_vblank.c])
			], [
				AC_MSG_RESULT(no)
			])
		])
	])
])
