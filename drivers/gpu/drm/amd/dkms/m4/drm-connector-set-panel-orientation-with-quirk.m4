dnl #
dnl # commit v5.5-rc2-1360-g69654c632d80
dnl # drm/connector: Split out orientation quirk detection (v2)
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_SET_PANEL_ORIENTATION_WITH_QUIRK], [
	AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE_SYMBOL([
                        #include <drm/drm_connector.h>
                ],[
                        drm_connector_set_panel_orientation_with_quirk(NULL, 0, 0, 0);
                ],[drm_connector_set_panel_orientation_with_quirk], [drivers/gpu/drm/drm_connector.c], [
                        AC_DEFINE(HAVE_DRM_CONNECTOR_SET_PANEL_ORIENTATION_WITH_QUIRK, 1,
                                [drm_connector_set_panel_orientation_with_quirk() is available])
                ])
        ])
])
