dnl #
dnl #commit aad0eab4e8dd76d1ba5248f9278633829cbcec38dnl
dnl #Author: Ville Syrjälä <ville.syrjala@linux.intel.com>
dnl #Date:   Wed Jan 25 19:26:33 2017 +0200
dnl #drm/dp_mst: Enable registration of AUX devices for MST ports
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_CONNECTOR_LATE_REGISTER],[
		AC_MSG_CHECKING([whether drm_dp_mst_connector_late_register() is available])
		AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <drm/drm_dp_mst_helper.h>
		], [
				drm_dp_mst_connector_late_register(NULL,NULL);
		], [drm_dp_mst_connector_late_register], [drivers/gpu/drm/drm_dp_mst_topology.c], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_DRM_DP_MST_CONNECTOR_LATE_REGISTER, 1, [whether drm_dp_mst_connector_late_register() is available])
		], [
				AC_MSG_RESULT(no)
		])
])
