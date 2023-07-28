dnl #
dnl # commit v5.18-rc2-597-g2a64b147350f
dnl # drm/display: Move DSC header and helpers into display-helper module
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DSC_COMPUTE_RC_PARAMETERS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
            #include <drm/drm_dsc.h>
		], [
			drm_dsc_compute_rc_parameters(NULL);
		], [
			AC_DEFINE(HAVE_DRM_DSC_COMPUTE_RC_PARAMETERS, 1,
				[drm_dsc_compute_rc_parameters() is available])
		])
	])
])
