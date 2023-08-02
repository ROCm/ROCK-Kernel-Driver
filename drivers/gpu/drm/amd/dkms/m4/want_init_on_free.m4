dnl #
dnl # v5.2-5754-g6471384af2a6
dnl # mm: security: introduce init_on_alloc=1 and init_on_free=1 boot options
dnl #
AC_DEFUN([AC_AMDGPU_WANT_INIT_ON_FREE], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
			#include <linux/mm.h>
		], [
			bool r;
                        r = want_init_on_free();
		], [
                        AC_DEFINE(HAVE_WANT_INIT_ON_FREE, 1,
                                [want_init_on_free() is available])
                ])
        ])
])