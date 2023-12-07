dnl #
dnl # v5.1-rc2-1103-ge33898a20744
dnl # drm/client: Rename drm_client_add() to drm_client_register()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CLIENT_REGISTER], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <drm/drm_client.h>
                ],[
                        drm_client_register(NULL);
                ],[
                        AC_DEFINE(HAVE_DRM_CLIENT_REGISTER, 1,
                                [drm_client_register() is available])
                ])
        ])
])