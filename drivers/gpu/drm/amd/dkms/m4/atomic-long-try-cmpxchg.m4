dnl #
dnl # v5.13-rc1-138-g67d1b0de258a locking/atomic: add arch_atomic_long*()
dnl #
AC_DEFUN([AC_AMDGPU_LINUX_ATOMIC_LONG_TRY_CMPXCHG], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/atomic.h>
                ], [
                        bool t;
			long r = 0;
                        t = atomic_long_try_cmpxchg(NULL, NULL, r);
                ], [
                        AC_DEFINE(HAVE_LINUX_ATOMIC_LONG_TRY_CMPXCHG, 1,
                                [atomic_long_try_cmpxchg() is available])
                ])
        ])
])

dnl #
dnl # v6.3-rc1-6-g8fc4fddaf9a1
dnl # locking/generic: Wire up local{,64}_try_cmpxchg()
dnl #
AC_DEFUN([AC_AMDGPU_LINUX_LOCAL_TRY_CMPXCHG], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <asm-generic/local.h>
                ], [
                        bool t;
                        s64 r = 0;
                        local_t *l = NULL;
                        t = local_try_cmpxchg(l, NULL, r);
                ], [
                        AC_DEFINE(HAVE_LINUX_LOCAL_TRY_CMPXCHG, 1,
                                [local_try_cmpchg() is available])
                ])
        ])
])
