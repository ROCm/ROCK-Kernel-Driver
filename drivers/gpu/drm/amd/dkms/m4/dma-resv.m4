dnl #
dnl # commit v5.3-rc1-449-g52791eeec1d9
dnl # dma-buf: rename reservation_object to dma_resv
dnl #
AC_DEFUN([AC_AMDGPU_DMA_RESV], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/dma-resv.h], [
			AC_DEFINE(HAVE_DMA_RESV_H, 1, [linux/dma-resv.h is available])

			dnl #
			dnl # commit v5.3-rc1-476-gb016cd6ed4b7
			dnl # dma-buf: Restore seqlock around dma_resv updates
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <linux/dma-resv.h>
			], [
				struct dma_resv *resv;
				write_seqcount_begin(&resv->seq);
			], [
				AC_DEFINE(HAVE_DMA_RESV_SEQ, 1,
					[dma_resv->seq is available])
			])
		],[
			dnl #
			dnl # commit v5.3-rc1-448-g5d344f58da76
			dnl # dma-buf: nuke reservation_object seq number
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <linux/reservation.h>
			], [
				struct reservation_object *resv;
				write_seqcount_begin(&resv->seq);
			], [
				dnl #
				dnl # commit v4.19-rc6-1514-g27836b641c1b
				dnl # dma-buf: remove shared fence staging in reservation object
				dnl #
				AC_KERNEL_TRY_COMPILE([
					#include <linux/reservation.h>
				], [
					struct reservation_object *resv;
					resv->staged = NULL;
				], [], [
					AC_DEFINE(HAVE_RESERVATION_OBJECT_DROP_STAGED, 1,
						[reservation_object->staged is dropped])
				])
			], [
				AC_DEFINE(HAVE_RESERVATION_OBJECT_DROP_SEQ, 1,
					[reservation_object->seq is dropped])
			])
		])
	])
])
