dnl #
dnl # commit 9a7c0da823fd4e65098bd466a996503cc8309c0e
dnl # drm/dp_mst: Add PBN calculation for DSC modes
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_CALC_PBN_MODE], [
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
			drm_dp_calc_pbn_mode(0, 0, 0);
		], [
			AC_DEFINE(HAVE_DRM_DP_CALC_PBN_MODE_3ARGS, 1,
				[drm_dp_calc_pbn_mode() wants 3args])
		])
	])
])
