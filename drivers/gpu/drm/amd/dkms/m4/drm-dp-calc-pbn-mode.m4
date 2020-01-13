dnl #
dnl # commit 9a7c0da823fd4e65098bd466a996503cc8309c0e
dnl # drm/dp_mst: Add PBN calculation for DSC modes
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_CALC_PBN_MODE],
	[AC_MSG_CHECKING([whether drm_dp_calc_pbn_mode() wants three arguments])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_dp_mst_helper.h>
	], [
		drm_dp_calc_pbn_mode(0, 0, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DP_CALC_PBN_MODE_3ARGS, 1, [drm_dp_calc_pbn_mode() wants 3args])
	], [
		AC_MSG_RESULT(no)
	])
])
