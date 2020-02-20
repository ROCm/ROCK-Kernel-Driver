dnl # commit 1bf6ad622b9be58484279978f85716fbb10d545b
dnl # Author: Daniel Vetter <daniel.vetter@ffwll.ch>
dnl # drm/vblank: drop the mode argument from drm_calc_vbltimestamp_from_scanoutpos
dnl #
AC_DEFUN([AC_AMDGPU_GET_SCANOUT_POSITION_IN_DRM_DRIVER],
	[AC_MSG_CHECKING([whether get_scanout_position return bool])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drmP.h>
		bool foo(struct drm_device *dev, unsigned int pipe,
			bool in_vblank_irq, int *vpos, int *hpos,
			ktime_t *stime, ktime_t *etime,
			const struct drm_display_mode *mode) { return 0; }
	], [
		struct drm_driver *bar = NULL;
		bar->get_scanout_position = foo;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_SCANOUT_POSITION_RETURN_BOOL, 1, [get_scanout_position return bool])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether get_scanout_position has struct drm_display_mode arg])
		dnl #
		dnl # commit 3bb403bf421b
		dnl # drm: Stop using linedur_ns and pixeldur_ns for vblank timestamps
		dnl #
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drmP.h>
			int foo(struct drm_device *dev, int crtc,
				unsigned int flags,
				int *vpos, int *hpos,
				ktime_t *stime, ktime_t *etime,
				const struct drm_display_mode *mode) { return 0; }
		], [
			struct drm_driver *bar = NULL;
			bar->get_scanout_position = foo;
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_GET_SCANOUT_POSITION_HAS_DRM_DISPLAY_MODE_ARG, 1, [get_scanout_position has struct drm_display_mode arg])
		], [
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING([whether get_scanout_position has timestamp arg])
			dnl #
			dnl # commit 8f6fce03ddaf
			dnl # drm: Push latency sensitive bits of vblank scanoutpos timestamping into kms drivers.
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
				int foo(struct drm_device *dev, int crtc,
					int *vpos, int *hpos, ktime_t *stime,
					ktime_t *etime) { return 0; }
			], [
				struct drm_driver *bar = NULL;
				bar->get_scanout_position = foo;
			],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_GET_SCANOUT_POSITION_HAS_TIMESTAMP_ARG, 1, [get_scanout_position has timestamp arg])
			], [
				AC_MSG_RESULT(no)
			])
		])
	])
])
