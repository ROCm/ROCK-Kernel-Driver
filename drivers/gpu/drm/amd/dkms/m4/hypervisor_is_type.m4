dnl #
dnl # commit: 79cc74155218316b9a5d28577c7077b2adba8e58
dnl # x86/paravirt: Provide a way to check for hypervisors
dnl #
AC_DEFUN([AC_AMDGPU_HYPERVISOR_IS_TYPE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/types.h>
			#include <asm/hypervisor.h>
		], [
			hypervisor_is_type(X86_HYPER_NATIVE);
		], [
			AC_DEFINE(HAVE_HYPERVISOR_IS_TYPE, 1,
				[hypervisor_is_type() is available])
		], [
		])
	])
])