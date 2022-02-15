dnl #
dnl # commit v5.15-rc2-1274-g710074bb8ab0
dnl # drm/probe-helper: add drm_kms_helper_connector_hotplug_event
dnl #
AC_DEFUN([AC_AMDGPU_DRM_KMS_HELPER_CONNECTOR_HOTPLUG_EVENT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_probe_helper.h>
		], [
			drm_kms_helper_connector_hotplug_event(NULL);
		], [
			AC_DEFINE(HAVE_DRM_KMS_HELPER_CONNECTOR_HOTPLUG_EVENT, 1,
				[drm_kms_helper_connector_hotplug_event() function is available])
		])
	])
])
