#ifndef AMDKCL_RESERVATION_H
#define AMDKCL_RESERVATION_H

#ifndef HAVE_DMA_RESV_H
#include <linux/dma-resv.h>
#define reservation_object dma_resv
#endif
#endif
