dnl #
dnl # v5.15-rc1-1-g12235da8c80a
dnl # kernel/locking: Add context to ww_mutex_trylock()
dnl #
AC_DEFUN([AC_AMDGPU_WW_MUTEX_TRYLOCK_CONTEXT_ARG], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
			#include <linux/ww_mutex.h>
		], [
			int r;
                        r = ww_mutex_trylock(NULL, NULL);
		], [
                        AC_DEFINE(HAVE_WW_MUTEX_TRYLOCK_CONTEXT_ARG, 1,
                                [ww_mutex_trylock() has context arg])
                ])
        ])
])
