dnl #
dnl # commit v5.18-rc2-597-g2a64b147350f
dnl # drm/display: Move DSC header and helpers into display-helper module
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DSC_PPS_PAYLOAD_PACK], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_dsc.h>
		], [
			drm_dsc_pps_payload_pack(NULL, NULL);
		], [
			AC_DEFINE(HAVE_DRM_DSC_PPS_PAYLOAD_PACK, 1,
				[drm_dsc_pps_payload_pack() is available])
		])
	])
])
