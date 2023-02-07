dnl #
dnl # commit 078dec3326e2244c62e8a8d970ba24359e3464be
dnl # dma-buf: add dma_fence_get_stub
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_GET_STUB], [
	AC_KERNEL_TRY_COMPILE([
		#include <linux/dma-fence.h>
	],[
		dma_fence_get_stub();
	],[
		AC_DEFINE(HAVE_DMA_FENCE_GET_STUB, 1,
			[whether dma_fence_get_stub exits])
	])
])

dnl #
dnl # commit f54d1867005c3323f5d8ad83eed823e84226c429
dnl # dma-buf: Rename struct fence to dma_fence
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_HEADERS], [
	AS_IF([test $HAVE_LINUX_DMA_FENCE_H], [
		AC_KERNEL_DO_BACKGROUND([
			AC_AMDGPU_DMA_FENCE_GET_STUB
		])
	], [
		dnl #
		dnl # commit b3dfbdf261e076a997f812323edfdba84ba80256
		dnl # dma-buf/fence: add fence_array fences v6
		dnl #
		AC_KERNEL_CHECK_HEADERS([linux/fence-array.h])
		AC_KERNEL_DO_BACKGROUND([
		])
	])
])
