dnl #
dnl # commit v6.3-5135-g7c5343f2a753
dnl # "drm/mst: Refactor the flow for payload allocation/removement"
dnl
AC_DEFUN([AC_AMDGPU_DRM_DP_REMOVE_RAYLOAD_PART], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_dp_mst_helper.h>
		], [
			drm_dp_remove_payload_part1(NULL, NULL, NULL);
		], [drm_dp_remove_payload_part1],[drivers/gpu/drm/display/drm_dp_mst_topology.c],[
			AC_DEFINE(HAVE_DRM_DP_REMOVE_RAYLOAD_PART, 1,
				[drm_dp_remove_payload_part{1,2}() is available])
		])
	])
])
