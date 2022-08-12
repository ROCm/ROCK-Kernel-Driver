dnl #
dnl # commit v5.18-2579-g3af4b1f1d6e7
dnl # "drm/dp_mst: add passthrough_aux to struct drm_dp_mst_port"
dnl
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_POST_PASSTHROUGH_AUX], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#if defined(HAVE_DRM_DISPLAY_DRM_DP_MST_HELPER_H)
			#include <drm/display/drm_dp_mst_helper.h>
			#elif defined(HAVE_DRM_DP_DRM_DP_MST_HELPER_H)
			#include <drm/dp/drm_dp_mst_helper.h>
			#else
			#include <drm/drm_dp_mst_helper.h>
			#endif
		], [
			struct drm_dp_mst_port *dp_mst_port = NULL;
			dp_mst_port->passthrough_aux = NULL;
		], [
			AC_DEFINE(HAVE_DRM_DP_MST_PORT_PASSTHROUGH_AUX, 1,
				[struct drm_dp_mst_port has passthrough_aux member])
		])
	])
])
