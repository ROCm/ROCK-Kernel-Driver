dnl #
dnl # commit 078dec3326e2244c62e8a8d970ba24359e3464be
dnl # dma-buf: add dma_fence_get_stub
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_GET_STUB],
	[AC_MSG_CHECKING([whether dma_fence_get_stub exits])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/dma-fence.h>
	],[
		dma_fence_get_stub();
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DMA_FENCE_GET_STUB, 1, [whether dma_fence_get_stub exits])
	],[
		AC_MSG_RESULT(no)
	])
])
