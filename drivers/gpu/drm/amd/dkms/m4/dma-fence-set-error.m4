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
AC_DEFUN([AC_AMDGPU_DMA_FENCE_SET_ERROR],
	[AC_MSG_CHECKING([whether dma_fence_set_error() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/dma-fence.h>
	],[
		dma_fence_set_error(NULL, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DMA_FENCE_SET_ERROR, 1, [dma_fence_set_error() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
