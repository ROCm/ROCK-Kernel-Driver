dnl #
dnl # v4.13-9285-gcd9e61ed1eeb
dnl # rbtree: cache leftmost node internally
dnl # 
dnl # v5.14-rc6-42-g089050cafa10
dnl # rbtree: Split out the rbtree type definitions into <linux/rbtree_types.h>
dnl # 
AC_DEFUN([AC_AMDGPU_RB_ROOT_CACHED], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/rbtree.h>
			#ifdef HAVE_LINUX_RBTREE_TYPES_H
                        #include <linux/rbtree_types.h>
			#endif
                ],[
                        struct rb_root_cached *r = NULL;
                ],[
                        AC_DEFINE(HAVE_RB_ROOT_CACHED, 1,
                                [struct rb_root_cached is available])
                ])
        ])
])

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



