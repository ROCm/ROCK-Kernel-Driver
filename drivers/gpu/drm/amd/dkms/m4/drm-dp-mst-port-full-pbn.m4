dnl #
dnl # commit v5.6-rc5-4-gfcf463807596
dnl # drm/dp_mst: Use full_pbn instead of available_pbn for bandwidth checks
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_PORT_FULL_PBN], [
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
			struct drm_dp_mst_port *mst_port = NULL;
			mst_port->full_pbn = 0;
		], [
			AC_DEFINE(HAVE_DRM_DP_MST_PORT_FULL_PBN, 1,
				[drm_dp_mst_port struct has full_pbn member])
		])
	])
])
