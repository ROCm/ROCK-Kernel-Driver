#ifndef __EMU_WRAPPER_H
#define __EMU_WRAPPER_H

#define vma_get_pgoff(v)		((v)->vm_pgoff)

#define PCI_SET_DMA_MASK(pdev,mask)	(((pdev)->dma_mask) = (mask))

#endif
