dnl #
dnl # commit: 03b2a320b19f1424e9ac9c21696be9c60b6d0d93
dnl # x86/virt: Add enum for hypervisors to replace x86_hyper
dnl #
AC_DEFUN([AC_AMDGPU_X86_HYPERVISOR_TYPE], [
    AC_KERNEL_DO_BACKGROUND([
        AC_KERNEL_TRY_COMPILE([
		#include <linux/types.h>
		#include <asm/hypervisor.h>
        ], [
		enum x86_hypervisor_type test;
		test = X86_HYPER_NATIVE;
        ], [
            AC_DEFINE(HAVE_X86_HYPERVISOR_TYPE, 1,
                [enum x86_hypervisor_type is available])
        ], [
        ])
    ])
])