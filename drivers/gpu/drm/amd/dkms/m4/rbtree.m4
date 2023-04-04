dnl #
dnl # v5.11-20-g2d24dd5798d0
dnl # rbtree: Add generic add and find helpers
dnl #
AC_DEFUN([AC_AMDGPU_RB_ADD_CACHED], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/rbtree.h>
                ],[
                        rb_add_cached(NULL, NULL, NULL);
                ],[
                        AC_DEFINE(HAVE_RB_ADD_CACHED, 1,
                                [rb_add_cached is available])
                ])
        ])
])



