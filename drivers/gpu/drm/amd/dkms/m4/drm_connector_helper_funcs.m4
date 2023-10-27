dnl #
dnl # v5.1-rc1-14-g9d2230dc1351
dnl # drm: writeback: Add job prepare and cleanup operations
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_HELPER_FUNCS_PREPARE_WRITEBACK_JOB], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <drm/drm_modeset_helper_vtables.h>
                ],[
                        struct drm_connector_helper_funcs *funcs = NULL;
                        funcs->prepare_writeback_job((struct drm_writeback_connector *)NULL, (struct drm_writeback_job *)NULL);
                ],[
                        AC_DEFINE(HAVE_DRM_CONNECTOR_HELPER_FUNCS_PREPARE_WRITEBACK_JOB, 1,
                                [drm_connector_helper_funcs->prepare_writeback_job is available])
                ])
        ])
])