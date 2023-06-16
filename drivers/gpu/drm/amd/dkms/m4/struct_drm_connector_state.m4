dnl #
dnl # commit v5.0-rc7-1020-gd2c6a405846c
dnl # drm: Add HDMI colorspace property
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_STATE_COLORSPACE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_connector.h>
		],[
			struct drm_connector_state *connector_state = NULL;
			connector_state->colorspace = 0;
		],[
			AC_DEFINE(HAVE_DRM_CONNECTOR_STATE_COLORSPACE, 1,
				[drm_connector_state->colorspace is available])
		])
	])
])

AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CONNECTOR_STATE], [
	AC_AMDGPU_DRM_CONNECTOR_STATE_COLORSPACE
])