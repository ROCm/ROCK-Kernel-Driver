dnl #
dnl # commit v5.6-rc5-4-gfcf463807596
dnl # drm/dp_mst: Use full_pbn instead of available_pbn for bandwidth checks
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_PORT_FULL_PBN], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_dp_mst_helper.h>
		], [
			struct drm_dp_mst_port *mst_port = NULL;
			mst_port->full_pbn = 0;
		], [
			AC_DEFINE(HAVE_DRM_DP_MST_PORT_FULL_PBN, 1,
				[drm_dp_mst_port struct has full_pbn member])
		])
	])
])
