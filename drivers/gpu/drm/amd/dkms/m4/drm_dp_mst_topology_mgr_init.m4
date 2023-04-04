dnl #
dnl # v4.10-rc3-517-g7b0a89a6db9a
dnl # drm/dp: Store drm_device in MST topology manager
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_TOPOLOGY_MGR_INIT], [
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
			drm_dp_mst_topology_mgr_init(NULL, (struct drm_device *)NULL, NULL, 0, 0, 0, 0, 0);
		], [
			AC_DEFINE(HAVE_DRM_DP_MST_TOPOLOGY_MGR_INIT_MAX_LANE_COUNT, 1,
				[drm_dp_mst_topology_mgr_init() has max_lane_count and max_link_rate])
		])
	])
])
