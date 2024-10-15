dnl #
dnl # commit v6.5-rc2-872-g113cdddcded6
dnl # drm/cec: add drm_dp_cec_attach() as the non-edid version of set edid
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_CEC_ATTACH], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/display/drm_dp_helper.h>
		], [
			drm_dp_cec_attach(NULL, 0);
		], [drm_dp_cec_attach], [drivers/gpu/drm/display/drm_dp_cec.c], [
			AC_DEFINE(HAVE_DRM_DP_CEC_ATTACH, 1,
				[drm_dp_cec_attach() is available])
		])
	])
])
