dnl #
dnl # 76250f2b743b72cb685cc51ac0cdabb32957180b
dnl # dma-buf/fence: Avoid use of uninitialised timestamp
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_FLAG_TIMESTAMP_BIT], [
        AC_KERNEL_TRY_COMPILE([
		#include <linux/dma-fence.h>
        ], [
		enum dma_fence_flag_bits flag = DMA_FENCE_FLAG_TIMESTAMP_BIT;
        ], [
                AC_DEFINE(HAVE_DMA_FENCE_FLAG_TIMESTAMP_BIT, 1,
                        [DMA_FENCE_FLAG_TIMESTAMP_BIT is available])
        ])
])
