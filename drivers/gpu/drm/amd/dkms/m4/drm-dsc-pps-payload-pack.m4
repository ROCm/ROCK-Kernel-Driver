dnl #
dnl # commit dbfbe717ccbb5b42815ef4bc35a66e2191b2e98d
dnl # drm/dsc: Split DSC PPS and SDP header initialisations
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DSC_PPS_PAYLOAD_PACK], [
	AC_MSG_CHECKING([whether drm_dsc_pps_payload_pack() is available])
	AC_KERNEL_CHECK_SYMBOL_EXPORT([drm_dsc_pps_payload_pack],[drivers/gpu/drm/drm_dsc.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DSC_PPS_PAYLOAD_PACK, 1, [drm_dsc_pps_payload_pack() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
