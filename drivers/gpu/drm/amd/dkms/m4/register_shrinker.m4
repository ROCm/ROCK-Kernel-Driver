dnl #
dnl # v5.16-rc1-22-g91f75eb481cf x86/MCE/AMD, EDAC/mce_amd: Support non-uniform MCA bank type enumeration
dnl #
AC_DEFUN([AC_AMDGPU_REGISTER_SHRINKER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/mm.h>
			#include <linux/sched/mm.h>
			#include <linux/shrinker.h>
		],[    
			struct shrinker *a = NULL;
			const char *b = NULL;
			register_shrinker(a, b);
		],[
			AC_DEFINE(HAVE_REGISTER_SHRINKER_WITH_TWO_ARGUMENTS, 1,
				[whether register_shrinker(x, x) is available])
		])	
	])
])

dnl #
dnl # commit: v6.6-rc4-53-gc42d50aefd17
dnl # mm: shrinker: add infrastructure for dynamically allocating shrinker
dnl # 
AC_DEFUN([AC_AMDGPU_SHRINKER_REGISTER], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE_SYMBOL([
                        #include <linux/shrinker.h>
                ], [
                        shrinker_register(NULL);
                ], [shrinker_register], [mm/shrinker.c], [
                        AC_DEFINE(HAVE_SHRINKER_REGISTER, 1,
                                [shrinker_register() is available])
                ])
        ])
])

AC_DEFUN([AC_AMDGPU_SHRINKER], [
		AC_AMDGPU_REGISTER_SHRINKER
		AC_AMDGPU_SHRINKER_REGISTER
])
