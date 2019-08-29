dnl #
dnl # commit c555f02371c338b06752577aebf738dbdb6907bd
dnl # drm: drop _mode_ from update_edit_property()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_UPDATE_EDID_PROPERTY], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_connector.h>
		],[
			drm_connector_update_edid_property(NULL, NULL);
		],[drm_connector_update_edid_property],[drivers/gpu/drm/drm_connector.c],[
			AC_DEFINE(HAVE_DRM_CONNECTOR_UPDATE_EDID_PROPERTY, 1,
				[drm_connector_update_edid_property() is available])
		])
	])
])
