dnl #
dnl # commit v5.4-rc4-863-g15fd552d186c
dnl # dma-buf: change DMA-buf locking convention v3
dnl #
AC_DEFUN([AC_AMDGPU_DMA_BUF_IS_DYNAMIC], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/dma-buf.h>
		],[
			dma_buf_is_dynamic(NULL);
		],[
			AC_DEFINE(HAVE_DMA_BUF_IS_DYNAMIC, 1,
				[dma_buf_is_dynamic() is available])
		])
	])
])