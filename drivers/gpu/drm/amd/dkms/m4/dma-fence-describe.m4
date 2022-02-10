dnl #
dnl # v5.15-rc2-1312-ga25efb3863d0
dnl # dma-buf: add dma_fence_describe and dma_resv_describe v2
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_DESCRIBE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/dma-fence.h>
		], [
			dma_fence_describe(NULL, NULL);
		], [
			AC_DEFINE(HAVE_DMA_FENCE_DESCRIBE, 1,
				[dma_fence_describe() is available])
		])
	])
])

