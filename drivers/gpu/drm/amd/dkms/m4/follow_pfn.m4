dnl #
dnl # v6.9-rc4-152-gcb10c28ac82c
dnl # mm: remove follow_pfn
dnl #
AC_DEFUN([AC_AMDGPU_FOLLOW_PFN], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/mm.h>
		],[
			follow_pfn(NULL, 0, NULL);
		],[follow_pfn], [mm/memory.c],[
			AC_DEFINE(HAVE_FOLLOW_PFN, 1,
				[follow_pfn() is available])
		])
	])
])

dnl #
dnl # v6.11-rc6-389-g6da8e9634bb7
dnl # mm: new follow_pfnmap API
dnl #
AC_DEFUN([AC_AMDGPU_FOLLOW_PFNMAP_START], [
    AC_KERNEL_DO_BACKGROUND([
        AC_KERNEL_TRY_COMPILE_SYMBOL([
            #include <linux/mm.h>
        ],[
            follow_pfnmap_start(NULL);
        ],[follow_pfnmap_start], [mm/memory.c],[
            AC_DEFINE(HAVE_FOLLOW_PFNMAP_START, 1,
                [follow_pfnmap_start() is available])
        ])
    ])
])

