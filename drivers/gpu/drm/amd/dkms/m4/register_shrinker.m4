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
