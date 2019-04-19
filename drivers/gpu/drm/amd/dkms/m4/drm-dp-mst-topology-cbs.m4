dnl #
dnl # commit 16bff572cc660f19e58c99941368dea050b36a05
dnl # drm/dp-mst-helper: Remove hotplug callback
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_TOPOLOGY_CBS],
	[AC_MSG_CHECKING([whether struct drm_dp_mst_topology_cbs has hotplug member])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_dp_mst_helper.h>
	], [
		struct drm_dp_mst_topology_cbs *dp_mst_cbs = NULL;
		dp_mst_cbs->hotplug(NULL);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DP_MST_TOPOLOGY_CBS_HOTPLUG, 1, [struct drm_dp_mst_topology_cbs has hotplug member])
	], [
		AC_MSG_RESULT(no)
	])
])
