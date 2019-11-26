dnl #
dnl # commit 13a3d91f17a5
dnl # drm: Pass 'name' to drm_encoder_init()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ENCODER_INIT_VALID_WITH_NAME],
	[AC_MSG_CHECKING([whether drm_encoder_init() wants name])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_encoder.h
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ENCODER_INIT_VALID_WITH_NAME, 1, [drm_encoder_init() wants name])
	],[
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_crtc.h>
	],[
		int error;

		error = drm_encoder_init(NULL, NULL, NULL, 0, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ENCODER_INIT_VALID_WITH_NAME, 1, [drm_encoder_init() wants name])
	],[
		AC_MSG_RESULT(no)
	])
	])
])
