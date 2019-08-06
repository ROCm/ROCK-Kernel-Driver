dnl # commit 1bf6ad622b9be58484279978f85716fbb10d545b
dnl # Author: Daniel Vetter <daniel.vetter@ffwll.ch>
dnl # drm/vblank: drop the mode argument from drm_calc_vbltimestamp_from_scanoutpos
dnl #
AC_DEFUN([AC_AMDGPU_GET_SCANOUT_POSITION_HAVE_FLAGS],
	[AC_MSG_CHECKING([whether get_scanout_position has flags])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drmP.h>
		int foo (struct drm_device *dev, unsigned int pipe,
			unsigned int flags, int *vpos, int *hpos,
			ktime_t *stime, ktime_t *etime,
			const struct drm_display_mode *mode)
		{
			return 0;
		}
	], [
		struct drm_driver *bar = NULL;
		bar->get_scanout_position = foo;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_SCANOUT_POSITION_HAVE_FLAGS, 1, [get_scanout_position has flags])
	], [
		AC_MSG_RESULT(no)
	])
])
