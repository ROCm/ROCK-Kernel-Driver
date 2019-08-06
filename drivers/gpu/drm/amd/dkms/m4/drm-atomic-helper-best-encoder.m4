dnl #
dnl # commit 9ecb549867d7f642f0379f574f0e52870009a8bf
dnl # drm/atomic: Add drm_atomic_helper_best_encoder()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_HELPER_BEST_ENCODER],
	[AC_MSG_CHECKING([whether drm_atomic_helper_best_encoder() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_atomic_helper.h>
	], [
		struct drm_connector *connector = NULL;
		struct drm_encoder *encoder = NULL;

		encoder = drm_atomic_helper_best_encoder(connector);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_BEST_ENCODER, 1, [drm_atomic_helper_best_encoder() is available])

	],[
		AC_MSG_RESULT(no)
	])
])
