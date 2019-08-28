dnl #
dnl # commit 299878bac326c890699c696ebba26f56fe93fc75
dnl # treewide: move set_memory_* functions away from cacheflush.h
dnl #
AC_DEFUN([AC_AMDGPU_SET_MEMORY_H],
	[AC_MSG_CHECKING([whether asm/set_memory.h is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/kernel.h>
		#ifdef CONFIG_X86
		#include <asm/set_memory.h>
		#endif
	],[
		#ifndef CONFIG_X86
		#error just check arch/x86/include/asm/set_memory.h
		#endif
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_MEMORY_H, 1, [asm/set_memory.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
