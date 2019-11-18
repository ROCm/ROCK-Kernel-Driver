dnl #
dnl # commit dc43332b7af6f7aecd6b8867caeab272d5934d60
dnl # drm/i915: Move dsc rate params compute into drm
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DSC_COMPUTE_RC_PARAMETERS], [
	AC_MSG_CHECKING([whether drm_dsc_compute_rc_parameters() is available])
	AC_KERNEL_CHECK_SYMBOL_EXPORT([drm_dsc_compute_rc_parameters],[drivers/gpu/drm/drm_dsc.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DSC_COMPUTE_RC_PARAMETERS, 1, [drm_dsc_compute_rc_parameters() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
