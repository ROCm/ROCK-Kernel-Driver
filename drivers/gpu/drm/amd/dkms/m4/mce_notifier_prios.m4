dnl #
dnl #
dnl # v5.5-rc2-5-g8438b84ab42d x86/mce: Take action on UCNA/Deferred errors again
dnl #
AC_DEFUN([AC_AMDGPU_MCE_PRIO_UC], [
        AC_KERNEL_TRY_COMPILE([
                #include <asm/mce.h>
        ], [
		enum mce_notifier_prios pri;
		pri = MCE_PRIO_UC;
        ], [
                AC_DEFINE(HAVE_MCE_PRIO_UC, 1,
                        [enum MCE_PRIO_UC is available])
        ])
])
