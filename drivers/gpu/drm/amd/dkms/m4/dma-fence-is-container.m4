dnl #
dnl # commit v5.17-rc2-229-g976b6d97c623
dnl # dma-buf: consolidate dma_fence subclass checking
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_IS_CONTAINER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/dma-fence.h>
		], [
			dma_fence_is_container(NULL);
		], [
			AC_DEFINE(HAVE_DMA_FENCE_IS_CONTAINER, 1, [dma_fence_is_container() is available])
		])
	])
])
