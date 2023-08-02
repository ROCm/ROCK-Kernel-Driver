dnl #
dnl # commit v6.0-rc2-7-g05a940656e1e
dnl # slab: Introduce kmalloc_size_roundup()
dnl #
AC_DEFUN([AC_AMDGPU_MM_KMALLOC_SIZE_ROUNDUP], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/slab.h>
                ], [
                        size_t a, b = 0;
                        a = kmalloc_size_roundup(b);
                ], [
                        AC_DEFINE(HAVE_KMALLOC_SIZE_ROUNDUP, 1,
                                [kmalloc_size_roundup is available])
                ])
        ])
])
