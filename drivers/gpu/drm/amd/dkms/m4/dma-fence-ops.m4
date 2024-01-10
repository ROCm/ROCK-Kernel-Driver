dnl #
dnl # v5.1-rc2-1115-g5e498abf1485
dnl # dma-buf: explicitely note that dma-fence-chains use 64bit seqno
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_OPS_USE_64BIT_SEQNO], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/dma-fence.h>
		], [
			struct dma_fence_ops *ops = NULL;
			ops->use_64bit_seqno = false;
		], [
			AC_DEFINE(HAVE_DMA_FENCE_OPS_USE_64BIT_SEQNO, 1,
				[struct dma_fence_ops has use_64bit_seqno field])
		])
	])
])
