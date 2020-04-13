dnl #
dnl # commit f54d1867005c3323f5d8ad83eed823e84226c429
dnl # dma-buf: Rename struct fence to dma_fence
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_HEADERS], [
	AS_IF([test $HAVE_LINUX_DMA_FENCE_H], [
		AC_KERNEL_DO_BACKGROUND([
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
