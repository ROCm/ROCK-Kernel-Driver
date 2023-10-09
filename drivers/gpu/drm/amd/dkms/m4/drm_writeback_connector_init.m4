dnl #
dnl # v5.18-rc2-67-g57b8280a0a41
dnl # drm: allow passing possible_crtcs to drm_writeback_connector_init()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_WRITEBACK_CONNECTOR_INIT], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE_SYMBOL([
                        #include <drm/drm_writeback.h>
                ],[
                        drm_writeback_connector_init(NULL, NULL, NULL, NULL, NULL, 0, 0);
                ],[drm_writeback_connector_init], [drivers/gpu/drm/drm_writeback.c],[
                        AC_DEFINE(HAVE_DRM_WRITEBACK_CONNECTOR_INIT_7_ARGS, 1,
                                [drm_writeback_connector_init() has 7 args])
                ])
        ])
])