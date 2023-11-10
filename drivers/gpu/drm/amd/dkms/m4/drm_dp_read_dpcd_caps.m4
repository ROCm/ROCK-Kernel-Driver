dnl #
dnl # commit v5.9-rc1-294-gb9936121d95b
dnl # drm/i915/dp: Extract drm_dp_read_dpcd_caps()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_READ_DPCD_CAPS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_dp_helper.h>
		], [
			drm_dp_read_dpcd_caps(NULL, NULL);
		], [drm_dp_read_dpcd_caps], [drivers/gpu/drm/display/drm_dp_helper.c drivers/gpu/drm/drm_dp_helper.c], [
			AC_DEFINE(HAVE_DRM_DP_READ_DPCD_CAPS, 1,
				[drm_dp_read_dpcd_caps() is available])
		])
	])
])
