dnl #
dnl # v4.18-rc1-6-g6883f81aac6f
dnl # pid: Implement PIDTYPE_TGID
dnl #
AC_DEFUN([AC_AMDGPU_PID_TYPE], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/pid.h>
                ], [
                        enum pid_type a;
                        a = PIDTYPE_TGID;
                ], [
                        AC_DEFINE(HAVE_PIDTYPE_TGID, 1,
                                [PIDTYPE is availablea])
                ])
        ])
])
