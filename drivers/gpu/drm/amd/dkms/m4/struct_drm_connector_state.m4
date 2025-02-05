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

dnl #
dnl # commit v6.10-rc1-219-gab52af4ba7c7
dnl # drm/connector: hdmi: Add Broadcast RGB property
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_STATE_HDMI_BROADCAST_RGB], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_connector.h>
		],[
			struct drm_connector_state *connector_state = NULL;
			connector_state->hdmi.broadcast_rgb = DRM_HDMI_BROADCAST_RGB_AUTO;
		],[
			AC_DEFINE(HAVE_DRM_CONNECTOR_STATE_HDMI_BROADCAST_RGB, 1,
				[drm_connector_state->hdmi.broadcast_rgb is available])
		])
	])
])

AC_DEFUN([AC_AMDGPU_STRUCT_DRM_CONNECTOR_STATE], [
	AC_AMDGPU_DRM_CONNECTOR_STATE_COLORSPACE
	AC_AMDGPU_DRM_CONNECTOR_STATE_HDMI_BROADCAST_RGB
])
