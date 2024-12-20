dnl #
dnl # v6.9-rc3-3-g4edd7e96a1f1
dnl # kfifo: add kfifo_out_linear{,_ptr}()
dnl #
AC_DEFUN([AC_AMDGPU_KFIFO_OUT_LINEAR], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/kfifo.h>
                ],[
                        static DEFINE_KFIFO(fifo, int, 2);
                        unsigned int ret = kfifo_out_linear(&fifo, 0, 0);
                ],[
                        AC_DEFINE(HAVE_KFIFO_OUT_LINEAR, 1,
                                [kfifo_out_linear() available])
                ])
        ])
])

