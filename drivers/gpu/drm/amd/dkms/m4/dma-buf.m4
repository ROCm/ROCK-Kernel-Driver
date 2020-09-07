dnl #
dnl # v5.6-rc5-1663-g09606b5446c2
dnl # dma-buf: add peer2peer flag
dnl #
AC_DEFUN([AC_AMDGPU_DMA_BUF], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/dma-buf.h>
		],[
			struct dma_buf_ops *ptr = NULL;
			ptr->allow_peer2peer = false;
		],[
			AC_DEFINE(HAVE_STRUCT_DMA_BUF_OPS_ALLOW_PEER2PEER,
				1,
				[struct dma_buf_ops->allow_peer2peer is available])
		])
	])
])
