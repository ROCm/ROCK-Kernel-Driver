dnl #
dnl # v5.19-rc1-22-525d6515604e
dnl # drm/amd/pm: use bitmap_{from,to}_arr32 where appropriate
dnl #
AC_DEFUN([AC_AMDGPU_BITMAP_TO_ARR32], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/bitmap.h>
                ],[
                        bitmap_to_arr32(NULL, NULL, 0);
                ],[
                        AC_DEFINE(HAVE_BITMAP_TO_ARR32, 1,
                                [bitmap_to_arr32() is available])
                ])
        ])
])
