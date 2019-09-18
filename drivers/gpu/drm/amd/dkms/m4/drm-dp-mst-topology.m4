AC_DEFUN([AC_AMDGPU_DRM_DP_MST_TOPOLOGY], [
	dnl #
	dnl # commit 1e797f556c616a42f1e039b1ff1d3c58f61b6104
	dnl # drm/dp: Split drm_dp_mst_allocate_vcpi
	dnl #
	dnl # Note: This autoconf only works with compiler flag -Werror
	dnl #       The interface types are specified in Hungarian notation
	dnl #
	AC_MSG_CHECKING([whether drm_dp_mst_allocate_vcpi() has p,p,i,i interface])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_dp_mst_helper.h>
	], [
		drm_dp_mst_allocate_vcpi(NULL, NULL, 1, 1);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DP_MST_ALLOCATE_VCPI_P_P_I_I, 1, [
			drm_dp_mst_allocate_vcpi() has p,p,i,i interface
		])
	], [
		AC_MSG_RESULT(no)
	])
])

