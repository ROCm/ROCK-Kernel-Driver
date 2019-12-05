dnl #
dnl # commit v5.3-rc1-449-g52791eeec1d9
dnl # dma-buf: rename reservation_object to dma_resv
dnl #
AC_DEFUN([AC_AMDGPU_DMA_RESV_H],
	[AC_MSG_CHECKING([whether linux/dma-resv.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/dma-resv.h],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DMA_RESV_H, 1, [linux/dma-resv.h is available])
	],[
		AC_MSG_RESULT(no)
	])
])
