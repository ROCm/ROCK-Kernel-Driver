dnl # commit 9edbf1fa600a2ef17c7553c2103d0055d0320d15
dnl # drm: Add API for capturing frame CRCs
dnl # Adds files and directories to debugfs for controlling and reading
dnl # frame CRCs, per CRTC
dnl #
AC_DEFUN([AC_AMDGPU_2ARGS_SET_CRC_SOURCE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		], [
			struct drm_crtc *crtc = NULL;
			int ret;

			ret = crtc->funcs->set_crc_source(NULL, NULL);
		], [
			AC_DEFINE(HAVE_2ARGS_SET_CRC_SOURCE, 1,
				[crtc->funcs->set_crc_source() wants 2 args])
		])
	])
])
