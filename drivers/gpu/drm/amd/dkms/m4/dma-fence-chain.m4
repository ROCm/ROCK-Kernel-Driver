dnl #
dnl # v5.13-rc3-1424-g440d0f12b52a
dnl # dma-buf: add dma_fence_chain_alloc/free v3
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_CHAIN_ALLOC], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/dma-fence-chain.h>
		], [
			dma_fence_chain_alloc();
		], [
			AC_DEFINE(HAVE_DMA_FENCE_CHAIN_ALLOC, 1,
				[dma_fence_chain_alloc() is available])
		])
	])
])

dnl #
dnl # v5.0-1331-g7bf60c52e093
dnl # dma-buf: add new dma_fence_chain container v7
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_CHAIN_STRUCT], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/dma-fence-chain.h>
                ], [
			struct dma_fence_chain *chain;
                        chain = NULL;
                ], [
                        AC_DEFINE(HAVE_STRUCT_DMA_FENCE_CHAIN, 1,
                                [struct dma_fence_chain is available])
                ])
        ])
])

dnl #
dnl # v5.17-rc2-233-g18f5fad275ef
dnl # dma-buf: add dma_fence_chain_contained helper
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_CHAIN_CONTAINED], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/dma-fence-chain.h>
                ], [
			dma_fence_chain_contained(NULL);
                ], [
                        AC_DEFINE(HAVE_DMA_FENCE_CHAIN_CONTAINED, 1,
                                [dma_fence_chain_contained() is available])
                ])
        ])
])

