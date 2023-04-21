dnl #
dnl # v5.6-rc2-1-g66630058e56b
dnl # sched/rt: Provide migrate_disable/enable() inlines
dnl #
AC_DEFUN([AC_AMDGPU_MIGRATE_DISABLE], [
        AC_KERNEL_DO_BACKGROUND([
               AC_KERNEL_TRY_COMPILE([
                       #include <linux/preempt.h>
                ],[
                        migrate_disable();
                ],[
                        AC_DEFINE(HAVE_MIGRATE_DISABLE, 1,
				[migrate_disable() is available])
                ])
        ])
])
