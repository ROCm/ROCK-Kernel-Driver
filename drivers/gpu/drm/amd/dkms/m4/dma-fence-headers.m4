dnl # commit a009e975da5c7d42a7f5eaadc54946eb5f76c9af
dnl # dma-fence: Introduce drm_fence_set_error() helper
dnl # The dma_fence.error field (formerly known as dma_fence.status) is an
dnl # optional field that may be set by drivers before calling
dnl # dma_fence_signal(). The field can be used to indicate that the fence was
dnl # completed in err rather than with success, and is visible to other
dnl # consumers of the fence and to userspace via sync_file.
dnl # This patch renames the field from status to error so that its meaning is
dnl # hopefully more clear (and distinct from dma_fence_get_status() which is
dnl # a composite between the error state and signal state) and adds a helper
dnl # that validates the preconditions of when it is suitable to adjust the
dnl # error field.
AC_DEFUN([AC_AMDGPU_DMA_FENCE_SET_ERROR], [
	AC_KERNEL_TRY_COMPILE([
		#include <linux/dma-fence.h>
	], [
		dma_fence_set_error(NULL, 0);
	], [
		AC_DEFINE(HAVE_DMA_FENCE_SET_ERROR, 1,
			[dma_fence_set_error() is available])
	])
])

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
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/dma-fence.h], [
			AC_DEFINE(HAVE_DMA_FENCE_DEFINED, 1,
				[fence is renamed to dma_fence])

			AC_AMDGPU_DMA_FENCE_SET_ERROR
			AC_AMDGPU_DMA_FENCE_GET_STUB
		], [
			dnl #
			dnl # commit b3dfbdf261e076a997f812323edfdba84ba80256
			dnl # dma-buf/fence: add fence_array fences v6
			dnl #
			AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/fence-array.h], [
				AC_DEFINE(HAVE_FENCE_ARRAY_H, 1,
					[fence-array.h is available])
			])
		])
	])
])
