dnl #
dnl # v6.1-rc1-146-g90b575f52c6a
dnl # drm/edid: detach debugfs EDID override from EDID property update
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_EDID_OVERRIDE], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <drm/drm_connector.h>
                ],[
			struct drm_connector *connector = NULL;
			connector->edid_override = NULL;
                ],[
                        AC_DEFINE(HAVE_DRM_CONNECTOR_EDID_OVERRIDE, 1,
                                [drm_connector->edid_override is available])
                ])
        ])
])



