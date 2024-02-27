dnl #
dnl # commit 05d249352f1ae909230c230767ca8f4e9fdf8e7b
dnl # drm/exec: Pass in initial # of objects
dnl #
AC_DEFUN([AC_AMDGPU_DRM_EXEC_INIT], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <drm/drm_exec.h>
                ], [
                        drm_exec_init(NULL, 0, 0);
                ], [
                        AC_DEFINE(HAVE_DRM_EXEC_INIT_3_ARGUMENTS, 1,
                                [drm_exec() has 3 arguments])
                ])
        ])
])

AC_DEFUN([AC_AMDGPU_DRM_EXEC], [
                AC_AMDGPU_DRM_EXEC_INIT
])
