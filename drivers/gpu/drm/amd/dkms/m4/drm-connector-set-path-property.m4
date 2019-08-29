dnl #
dnl # commit 97e14fbeb53fe060c5f6a7a07e37fd24c087ed0c
dnl # drm: drop _mode_ from remaining connector functions
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_SET_PATH_PROPERTY], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_connector.h>
		],[
			drm_connector_set_path_property(NULL, NULL);
		],[drm_connector_set_path_property],[drivers/gpu/drm/drm_connector.c],[
			AC_DEFINE(HAVE_DRM_CONNECTOR_SET_PATH_PROPERTY, 1,
				[drm_connector_set_path_property() is available])
		])
	])
])
