dnl #
dnl # v5.7-rc2-1263-gc6af13d33475
dnl # timer: add fsleep for flexible sleeping
dnl #
AC_DEFUN([AC_AMDGPU_FSLEEP], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/delay.h>
                ], [
                        unsigned long usecs = 0;
                        fsleep(usecs);
                ], [
                        AC_DEFINE(HAVE_FSLEEP, 1,
                                [fsleep() is available])
                ])
        ])
])
