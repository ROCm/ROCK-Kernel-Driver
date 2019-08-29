dnl #
dnl # commit cde4c44d8769c1be16074c097592c46c7d64092b
dnl # drm: drop _mode_ from drm_mode_connector_attach_encode
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_ATTACH_ENCODER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_connector.h>
		],[
			drm_connector_attach_encoder(NULL, NULL);
		],[drm_connector_attach_encoder],[drivers/gpu/drm/drm_connector.c],[
			AC_DEFINE(HAVE_DRM_CONNECTOR_ATTACH_ENCODER, 1,
				[drm_connector_attach_encoder() is available])
		])
	])
])
