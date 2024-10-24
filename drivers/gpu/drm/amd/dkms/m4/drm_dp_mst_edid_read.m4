dnl #
dnl # commit v6.4-rc2-497-gc1c9042b2003
dnl # drm/display/dp_mst: convert to struct drm_edid
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_EDID_READ], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/display/drm_dp_mst_helper.h>
		], [
			drm_dp_mst_edid_read(NULL, NULL, NULL);
		], [drm_dp_mst_edid_read], [drivers/gpu/drm/display/drm_dp_mst_topology.c], [
			AC_DEFINE(HAVE_DRM_DP_MST_EDID_READ, 1,
				[drm_dp_mst_edid_read() is available])
		])
	])
])
