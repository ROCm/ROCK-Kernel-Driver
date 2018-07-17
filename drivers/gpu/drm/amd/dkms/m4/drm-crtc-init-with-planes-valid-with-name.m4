dnl #
dnl # 4.5 API change,
dnl # The function drm_crtc_init_with_planes now must be passed a name.
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CRTC_INIT_WITH_PLANES_VALID_WITH_NAME],
	[AC_MSG_CHECKING([whether drm_crtc_init_with_planes() wants name])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_crtc.h>
	],[
		struct drm_device *dev = NULL;
		struct drm_crtc *crtc = NULL;
		struct drm_plane *primary = NULL;
		struct drm_plane *cursor = NULL;
		const struct drm_crtc_funcs *funcs = NULL;
		const char *name = NULL;
		int error;

		error = drm_crtc_init_with_planes(dev, crtc, primary,
			cursor, funcs, name);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_CRTC_INIT_WITH_PLANES_VALID_WITH_NAME, 1, [drm_crtc_init_with_planes() wants name])
	],[
		AC_MSG_RESULT(no)
	])
])
