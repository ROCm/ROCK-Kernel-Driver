dnl #
dnl # commit v5.15-rc2-1273-g0d6a8c5e9683
dnl # drm/sysfs: introduce drm_sysfs_connector_hotplug_event
dnl #
AC_DEFUN([AC_AMDGPU_DRM_SYSFS_CONNECTOR_HOTPLUG_EVENT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_sysfs.h>
		], [
			drm_sysfs_connector_hotplug_event(NULL);
		], [
			AC_DEFINE(HAVE_DRM_SYSFS_CONNECTOR_HOTPLUG_EVENT, 1,
				[drm_sysfs_connector_hotplug_event() function is available])
		])
	])
])
