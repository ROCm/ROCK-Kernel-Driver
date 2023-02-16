dnl #
dnl # v5.18-rc1-237-g047a1b877ed4
dnl # dma-buf & drm/amdgpu: remove dma_resv workaround
dnl #
AC_DEFUN([AC_AMDGPU_DMA_RESV_FENCES], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/dma-resv.h>
		], [
			struct dma_resv *resv = NULL;
			resv->fences = NULL;
		], [
			dnl # this is the latest kernel
			AC_DEFINE(HAVE_DMA_RESV_FENCES, 1,[dma_resv->fences is available])
		], [
			dnl #
			dnl # v5.8-rc6-36-gcd29f22019ec dma-buf: Use sequence counter with associated wound/wait mutex
			dnl # v5.8-rc6-35-g318ce71f3e3a dma-buf: Remove custom seqcount lockdep class key
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <linux/dma-resv.h>
			], [
					struct dma_resv *obj = NULL;
					seqcount_ww_mutex_init(&obj->seq, &obj->lock);
			], [
					AC_DEFINE(HAVE_DMA_RESV_SEQCOUNT_WW_MUTEX_T, 1, [dma_resv->seq is seqcount_ww_mutex_t])
			], [
					dnl #
					dnl # v5.3-rc1-476-gb016cd6ed4b7 dma-buf: Restore seqlock around dma_resv updates
					dnl # v5.3-rc1-449-g52791eeec1d9 dma-buf: rename reservation_object to dma_resv
					dnl # v5.3-rc1-448-g5d344f58da76 dma-buf: nuke reservation_object seq number
					dnl #
					AC_KERNEL_TRY_COMPILE([
						#ifdef HAVE_LINUX_DMA_RESV_H
						#include <linux/dma-resv.h>
						#else
						#include <linux/reservation.h>
						#endif
					], [
						#ifdef HAVE_LINUX_DMA_RESV_H
						struct dma_resv *resv = NULL;
						#else
						struct reservation_object *resv = NULL;
						#endif
						write_seqcount_begin(&resv->seq);
					], [
						AC_DEFINE(HAVE_DMA_RESV_SEQ, 1,[dma_resv->seq is available])
					],[
						dnl #
						dnl # Trigger the bug for dma_resv->seq definition
						dnl #
						AC_DEFINE(HAVE_DMA_RESV_SEQ_BUG, 1, [Reporting dma_resv->seq bug])
			])
		])
		])
	])
])

AC_DEFUN([AC_AMDGPU_DMA_RESV], [
	AC_AMDGPU_DMA_RESV_FENCES
])
