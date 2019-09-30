dnl #
dnl # commit df6b35f409af0a8ff1ef62f552b8402f3fef8665
dnl # x86/fpu: Rename i387.h to fpu/api.h
dnl #
AC_DEFUN([AC_AMDGPU_ASM_FPU_API_H], [
	AC_MSG_CHECKING([whether asm/fpu/api.h is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/kernel.h>
		#ifdef CONFIG_X86
		#include <asm/fpu/api.h>
		#endif
	],[
		#ifndef CONFIG_X86
		#error just check arch/x86/include/asm/fpu/api.h
		#endif
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ASM_FPU_API_H, 1, [asm/fpu/api.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
