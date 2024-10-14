dnl #
dnl # commit v5.10-rc3-244-gdd8088d5a896
dnl # PM: runtime: Add pm_runtime_resume_and_get to deal with usage counter
dnl #
AC_DEFUN([AC_AMDGPU_PM_RUNTIME_RESUME_AND_GET], [
    AC_KERNEL_DO_BACKGROUND([
        AC_KERNEL_TRY_COMPILE([
            #include <linux/pm_runtime.h>
        ],[
            pm_runtime_resume_and_get(NULL);
        ],[
            AC_DEFINE(HAVE_PM_RUNTIME_RESUME_AND_GET, 1,
                [pm_runtime_resume_and_get() is available])
        ])
    ])
])
