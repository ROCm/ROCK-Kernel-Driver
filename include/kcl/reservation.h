#ifndef AMDKCL_RESERVATION_H
#define AMDKCL_RESERVATION_H

#ifndef HAVE_DMA_RESV_H
#include <linux/dma-resv.h>
#define reservation_object dma_resv

#if !defined(HAVE_RESERVATION_OBJECT_DROP_SEQ) && \
	!defined(HAVE_RESERVATION_OBJECT_DROP_STAGED)
static inline void
reservation_object_fini(struct reservation_object *obj)
{
	dma_resv_fini(obj);
	kfree(obj->staged);
}
#define amddma_resv_fini reservation_object_fini
#endif /* !HAVE_RESERVATION_OBJECT_DROP_SEQ/STAGED */
#endif /* HAVE_DMA_RESV_H */
#endif
