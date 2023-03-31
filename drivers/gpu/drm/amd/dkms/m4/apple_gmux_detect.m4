dnl #
dnl # v6.1-2256-gbd100f492c7e
dnl # platform/x86: apple-gmux: Add apple_gmux_detect() helper
dnl #
AC_DEFUN([AC_AMDGPU_APPLE_GMUX_DETECT], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/apple-gmux.h>
                ],[
                        apple_gmux_detect(NULL, NULL);
                ],[
                        AC_DEFINE(HAVE_APPLE_GMUX_DETECT, 1,
                                [apple_gmux_detect() is available])
                ])
        ])
])
