dnl #
dnl # commit f54d1867005c3323f5d8ad83eed823e84226c429
dnl # dma-buf: Rename struct fence to dma_fence
dnl #
AC_DEFUN([AC_AMDGPU_DMA_FENCE_HEADERS],
	[AC_MSG_CHECKING([whether fence is renamed to dma_fence])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/dma-fence.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DMA_FENCE_DEFINED, 1, [fence is renamed to dma_fence])

		AC_AMDGPU_DMA_FENCE_SET_ERROR
		AC_AMDGPU_DMA_FENCE_GET_STUB
	], [
		AC_MSG_RESULT(no)
dnl #
dnl # commit b3dfbdf261e076a997f812323edfdba84ba80256
dnl # dma-buf/fence: add fence_array fences v6
dnl #
		AC_MSG_CHECKING([whether fence_array.h is available])
		AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/fence-array.h],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_FENCE_ARRAY_H, 1, [fence-array.h is available])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
