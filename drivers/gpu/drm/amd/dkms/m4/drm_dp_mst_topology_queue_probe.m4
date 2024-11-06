dnl #
dnl # commit v6.10-3523-gb1c1c23eae66
dnl # drm/dp_mst: Add a helper to queue a topology probe
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_TOPOLOGY_QUEUE_PROBE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/display/drm_dp_mst_helper.h>
		], [
			drm_dp_mst_topology_queue_probe(NULL);
		], [drm_dp_mst_topology_queue_probe], [drivers/gpu/drm/display/drm_dp_mst_topology.c], [
			AC_DEFINE(HAVE_DRM_DP_MST_TOPOLOGY_QUEUE_PROBE, 1,
				[drm_dp_mst_topology_queue_probe() is available])
		])
	])
])
