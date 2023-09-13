dnl #
dnl # v6.4-rc1-190-g3f09a0cd4ea3:drm: Add common fdinfo helper
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FILE_DRM_SHOW_FDINFO], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE_SYMBOL([
                        #include <drm/drm_file.h>
                ],[
                        drm_show_fdinfo(NULL, NULL);
                ],[drm_show_fdinfo], [drivers/gpu/drm/drm_file.c], [
                        AC_DEFINE(HAVE_DRM_SHOW_FDINFO, 1, [drm_show_fdinfo() is available])
                ])
        ])
])

dnl #
dnl # v6.4-rc1-190-g3f09a0cd4ea3:drm: Add common fdinfo helper
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DRIVER_SHOW_FDINFO], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <drm/drm_drv.h>
                ], [
                        struct drm_driver *drm_driver = NULL;

                        drm_driver->show_fdinfo(NULL, NULL);
                ], [
                        AC_DEFINE(HAVE_DRM_DRIVER_SHOW_FDINFO, 1,
                                [drm_driver->show_fdinfo() is available])
                ])
        ])
])

AC_DEFUN([AC_AMDGPU_DRM_SHOW_FDINFO], [
		AC_AMDGPU_DRM_FILE_DRM_SHOW_FDINFO
		AC_AMDGPU_DRM_DRIVER_SHOW_FDINFO
])
