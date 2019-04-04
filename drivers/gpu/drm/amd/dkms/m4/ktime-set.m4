dnl # commit b17b20d70dcbe48dd1aa6aba073a60ddfce5d7db
dnl # Author: John Stultz <john.stultz@linaro.org>
dnl # Date:   Wed Jul 16 21:03:56 2014 +0000
dnl #
dnl # ktime: Change ktime_set() to take 64bit seconds value
dnl #
AC_DEFUN([AC_AMDGPU_KTIME_SET],
[AC_MSG_CHECKING([whether ktime_set() to take 64bit seconds value])
       AC_KERNEL_TRY_COMPILE([
               #include <linux/ktime.h>
       ], [
               ktime_set(0, 0);
       ],[
               AC_MSG_RESULT(yes)
               AC_DEFINE(HAVE_KTIME_SET, 1, [ktime_set() to take 64bit seconds value])
       ],[
               AC_MSG_RESULT(no)
       ])
])
