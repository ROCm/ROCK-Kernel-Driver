dnl #
dnl # v3.12-8403-g498d319bb512
dnl # kfifo API type safety
dnl #
AC_DEFUN([AC_AMDGPU_KFIFO_PUT], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/kfifo.h>
                ],[
                        static DEFINE_KFIFO(fifo, int, 2);
                        kfifo_put(&fifo, 0);
                ],[
                        AC_DEFINE(HAVE_KFIFO_PUT_NON_POINTER, 1,
                                [kfifo_put() have non pointer parameter])
                ])
        ])
])



