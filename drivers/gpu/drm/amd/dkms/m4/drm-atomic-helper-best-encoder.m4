dnl #
dnl # commit 9ecb549867d7f642f0379f574f0e52870009a8bf
dnl # drm/atomic: Add drm_atomic_helper_best_encoder()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_HELPER_BEST_ENCODER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_atomic_helper.h>
		], [
			struct drm_connector *connector = NULL;
			struct drm_encoder *encoder = NULL;

			encoder = drm_atomic_helper_best_encoder(connector);
		],[
			AC_DEFINE(HAVE_DRM_ATOMIC_HELPER_BEST_ENCODER, 1,i
				[drm_atomic_helper_best_encoder() is available])
		])
	])
])
