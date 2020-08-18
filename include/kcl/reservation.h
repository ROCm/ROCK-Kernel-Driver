/* SPDX-License-Identifier: MIT */
#ifndef KCL_RESERVATION_H
#define KCL_RESERVATION_H

#ifndef HAVE_LINUX_DMA_RESV_H
#include <kcl/kcl_reservation.h>

#if defined(HAVE_RESERVATION_OBJECT_STAGED)
static inline void
reservation_object_fini(struct reservation_object *obj)
{
	dma_resv_fini(obj);
	kfree(obj->staged);
}
#define amddma_resv_fini reservation_object_fini
#endif /* HAVE_RESERVATION_OBJECT_STAGED */
#endif /* HAVE_LINUX_DMA_RESV_H */
#endif
