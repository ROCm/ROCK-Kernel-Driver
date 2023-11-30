dnl #
dnl # commit v6.6-rc1-33-gb83ce9cb4a46
dnl # dma-buf: add dma_fence_timestamp helper
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_TIMESTAMP], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/dma-fence.h>
                ], [
			ktime_t time;
                        time = dma_fence_timestamp(NULL);
                ], [
                        AC_DEFINE(HAVE_DMA_FENCE_TIMESTAMP, 1, [dma_fence_TIMESTAMP() is available])
                ])
        ])
])

