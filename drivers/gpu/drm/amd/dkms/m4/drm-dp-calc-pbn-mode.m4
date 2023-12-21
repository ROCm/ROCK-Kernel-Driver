dnl #
dnl # commit v5.5-rc2-902-gdc48529fb14e
dnl # drm/dp_mst: Add PBN calculation for DSC modes
dnl #
dnl #v6.6-rc2-668-g7707dd602259
dnl #drm/dp_mst: Fix fractional DSC bpp handling
dnl
AC_DEFUN([AC_AMDGPU_DRM_DP_CALC_PBN_MODE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_dp_mst_helper.h>
		], [
			drm_dp_calc_pbn_mode(0, 0, 0);
		], [
			AC_DEFINE(HAVE_DRM_DP_CALC_PBN_MODE_3ARGS, 1,
				[drm_dp_calc_pbn_mode() wants 3 args])
		])
	])
])
