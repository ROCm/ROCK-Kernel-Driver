dnl #
dnl # commit v5.4-rc4-863-g15fd552d186c
dnl # dma-buf: change DMA-buf locking convention v3
dnl #
AC_DEFUN([AC_AMDGPU_DMA_BUF_OPS_DYNAMIC_MAPPING], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/dma-buf.h>
		], [
			struct dma_buf_ops *dma_buf_ops = NULL;
			dma_buf_ops->dynamic_mapping = TRUE;
		],[
			AC_DEFINE(HAVE_DMA_BUF_OPS_DYNAMIC_MAPPING, 1,
				[dma_buf dynamic_mapping is available])
		])
	])
])
