dnl #
dnl # commit 7dea19f9ee636cb244109a4dba426bbb3e5304b7
dnl # mm: introduce memalloc_nofs_{save,restore} API
dnl #
AC_DEFUN([AC_AMDGPU_MMAP_ASSERT_WRITE_LOCKED], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/mmap_lock.h>
                ], [
                        mmap_assert_write_locked(NULL);
                ], [
                        AC_DEFINE(HAVE_MMAP_ASSERT_WRITE_LOCKED, 1,
                                [mmap_assert_write_locked() is  available])
                ])
        ])
])

