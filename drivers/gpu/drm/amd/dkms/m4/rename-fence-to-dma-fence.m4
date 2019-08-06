dnl #
dnl # commit f54d1867005c3323f5d8ad83eed823e84226c429
dnl # dma-buf: Rename struct fence to dma_fence
dnl #
AC_DEFUN([AC_AMDGPU_RENAME_FENCE_TO_DMA_FENCE],
	[AC_MSG_CHECKING([whether fence is renamed to dma_fence])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/dma-fence.h>
	],[
		dma_fence_is_later(NULL, NULL);
		dma_fence_context_alloc(0);
		dma_fence_get_rcu_safe(NULL);
		dma_fence_init(NULL, NULL, NULL, 0, 0);
		dma_fence_wait_any_timeout(NULL,0,0,0,NULL);
		dma_fence_wait_timeout(NULL, 0, 0);
		dma_fence_is_later(NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(RENAME_FENCE_TO_DMA_FENCE, 1, [fence is renamed to dma_fence])
	],[
		AC_MSG_RESULT(no)
	])
])
