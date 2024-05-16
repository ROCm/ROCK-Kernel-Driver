dnl #
dnl #
dnl # v5.5-rc2-5-g8438b84ab42d x86/mce: Take action on UCNA/Deferred errors again
dnl #
AC_DEFUN([AC_AMDGPU_MCE_PRIO_UC], [
        AC_KERNEL_DO_BACKGROUND([
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
])
dnl #
dnl # v5.13-rc3-1-g94a311ce248e
dnl # x86/MCE/AMD, EDAC/mce_amd: Add new SMCA bank types
dnl #
AC_DEFUN([AC_AMDGPU_SMCA_UMC_V2], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <asm/mce.h>
                ], [
                        enum smca_bank_types bank_type;
                        bank_type = SMCA_UMC_V2;
                ], [
                        AC_DEFINE(HAVE_SMCA_UMC_V2, 1,
                                [enum SMCA_UMC_V2 is available])
                ])
        ])
])
