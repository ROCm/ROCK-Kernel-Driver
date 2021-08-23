dnl #
dnl # v5.6-rc2-335-gbb42df4662a4
dnl # dma-buf: add dynamic DMA-buf handling v15
dnl #
AC_DEFUN([AC_AMDGPU_DMA_BUF_UNPIN], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/dma-buf.h>
		],[
			dma_buf_unpin(NULL);
		],[
			AC_DEFINE(HAVE_DMA_BUF_UNPIN, 1,
				[dma_buf_unpin() is available])
		])
	])
])

AC_DEFUN([AC_AMDGPU_STRUCT_DMA_BUF_OPS_UNPIN], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/dma-buf.h>
		],[
			struct dma_buf_ops *ptr = NULL;
			ptr->unpin(NULL);
		],[
			AC_DEFINE(HAVE_STRUCT_DMA_BUF_OPS_UNPIN, 1,
				[struct dma_buf_ops->unpin() is available])
		])
	])
])




